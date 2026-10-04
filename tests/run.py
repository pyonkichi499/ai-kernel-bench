#!/usr/bin/env python3
"""統合テストランナー。

tests/integration/*.toml に書かれたテストケースごとに QEMU でカーネルを起動し、
シリアル出力と QEMU の終了コードで合否を判定する。カーネルの実装言語に依存しないので、
全言語の版で同じテストを使う。

テストケースの書式（TOML）:
    name        = "m0_boot"              # ケース名（ファイル名と同じにする）
    description = "通常起動できる"        # 何を確かめるか
    milestone   = "M0"                   # どのマイルストーンで追加したか
    kernel      = "main"                 # "main"（通常のカーネル）か "ktest"（テスト用カーネル）
    cmdline     = ""                     # カーネルのコマンドライン
    timeout     = 30                     # 秒。これを過ぎたら失敗
    expect      = ["正規表現", ...]       # この順にシリアル出力に現れること
    forbid      = ["PANIC", ...]         # 1 つでも現れたら失敗
    exit        = "any"                  # "any" | "success"（終了コード 33）| "failure"（35）

exit が "any" のケースは、expect が全部見つかった時点で QEMU を止めて合格とする。
"success" / "failure" のケースは、さらにカーネルが QEMU を終了させるのを待ち、終了コードを確かめる。

使い方（通常は make test-integration / make test-ktest から呼ぶ）:
    python3 tests/run.py --kernel c --iso build/c/kui.iso
    python3 tests/run.py --kernel c --iso build/c/kui.iso --only m0_boot
"""

from __future__ import annotations

import argparse
import os
import queue
import re
import sys
import threading
import time
import tomllib
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import qemu  # noqa: E402  (scripts/qemu.py)

# 言語ごとのカーネル名（ルート Makefile の NAME_<言語> と対応）
KERNEL_NAMES = {"c": "kui"}

# 各項目の型（TOML から読んだ値を検査する）
KEY_TYPES: dict[str, type | tuple[type, ...]] = {
    "name": str,
    "description": str,
    "milestone": str,
    "kernel": str,
    "cmdline": str,
    "timeout": (int, float),
    "expect": list,
    "forbid": list,
    "exit": str,
}
EXIT_CODES = {"success": qemu.EXIT_SUCCESS, "failure": qemu.EXIT_FAILURE}


@dataclass
class Case:
    name: str
    path: Path
    description: str = ""
    milestone: str = ""
    kernel: str = "main"
    cmdline: str = ""
    timeout: float = 30
    expect: list[str] = field(default_factory=list)
    forbid: list[str] = field(default_factory=list)
    exit: str = "any"


@dataclass
class Result:
    case: Case
    passed: bool
    reason: str
    seconds: float
    log_path: Path
    log: list[str]


def load_case(path: Path) -> Case:
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as e:
        raise ValueError(f"{path}: TOML として読めません: {e}") from e
    unknown = set(data) - set(KEY_TYPES)
    if unknown:
        raise ValueError(f"{path}: 未知の項目があります: {', '.join(sorted(unknown))}")
    if "name" not in data:
        raise ValueError(f"{path}: name がありません")
    for key, value in data.items():
        # bool は int の一種なので、timeout = true のような誤りを別に弾く
        if not isinstance(value, KEY_TYPES[key]) or isinstance(value, bool):
            raise ValueError(f"{path}: {key} の型が正しくありません: {value!r}")
    for key in ("expect", "forbid"):
        if not all(isinstance(v, str) for v in data.get(key, [])):
            raise ValueError(f"{path}: {key} は文字列の配列にしてください")
    case = Case(path=path, **data)
    if case.name != path.stem:
        raise ValueError(f"{path}: name（{case.name}）はファイル名と同じにしてください")
    if case.kernel not in ("main", "ktest"):
        raise ValueError(f"{path}: kernel は main か ktest です")
    if case.exit not in ("any", "success", "failure"):
        raise ValueError(f"{path}: exit は any / success / failure のどれかです")
    if case.timeout <= 0:
        raise ValueError(f"{path}: timeout は正の数にしてください")
    for pattern in case.expect + case.forbid:
        try:
            re.compile(pattern)
        except re.error as e:
            raise ValueError(f"{path}: 正規表現 /{pattern}/ が不正です: {e}") from e
    return case


def run_case(case: Case, iso: Path, kernel_name: str, log_dir: Path) -> Result:
    """1 つのケースを実行する。予期しない例外もケースの失敗として扱う。"""
    started = time.monotonic()
    try:
        return _run_case(case, iso, kernel_name, log_dir)
    except Exception as e:  # noqa: BLE001  ランナー自体は止めずに、失敗として報告する
        return Result(case, False, f"ランナーで例外が起きました: {e!r}", time.monotonic() - started,
                      log_dir / f"{case.name}.log", [])


def _run_case(case: Case, iso: Path, kernel_name: str, log_dir: Path) -> Result:
    elf = f"{kernel_name}-ktest.elf" if case.kernel == "ktest" else f"{kernel_name}.elf"
    expects = [re.compile(p) for p in case.expect]
    forbids = [re.compile(p) for p in case.forbid]
    log: list[str] = []
    started = time.monotonic()
    deadline = started + case.timeout
    log_path = log_dir / f"{case.name}.log"

    with qemu.QemuRun(iso, kernel_path=f"/boot/{elf}", cmdline=case.cmdline) as run:
        # シリアル出力は別スレッドで読み、キュー経由で受け取る（タイムアウトを扱うため）
        lines: queue.Queue[str | None] = queue.Queue()

        def reader() -> None:
            while (line := run.readline()) is not None:
                lines.put(line)
            lines.put(None)

        threading.Thread(target=reader, daemon=True).start()

        def finish(passed: bool, reason: str) -> Result:
            run.stop()
            log_path.write_text("\n".join(log) + "\n", encoding="utf-8")
            return Result(case, passed, reason, time.monotonic() - started, log_path, log)

        next_expect = 0
        exited = False
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                if next_expect < len(expects):
                    return finish(False, f"タイムアウト（{case.timeout} 秒）: /{case.expect[next_expect]}/ が出ませんでした")
                return finish(False, f"タイムアウト（{case.timeout} 秒）: QEMU が終了しませんでした")
            try:
                line = lines.get(timeout=min(remaining, 0.5))
            except queue.Empty:
                continue

            if line is None:
                exited = True
            else:
                log.append(line)
                for pattern in forbids:
                    if pattern.search(line):
                        return finish(False, f"禁止パターン /{pattern.pattern}/ が出ました: {line}")
                if next_expect < len(expects) and expects[next_expect].search(line):
                    next_expect += 1

            all_found = next_expect == len(expects)
            if all_found and case.exit == "any":
                return finish(True, "")
            if exited:
                code = run.proc.wait()
                if not all_found:
                    return finish(False, f"QEMU が終了しました（終了コード {code}）が、/{case.expect[next_expect]}/ が出ていません")
                want = EXIT_CODES[case.exit] if case.exit != "any" else None
                if want is not None and code != want:
                    return finish(False, f"終了コードが {code} でした（期待値 {want}: {case.exit}）")
                return finish(True, "")


def default_jobs() -> int:
    """同時に起動する QEMU の数の既定値。

    os.cpu_count() はマシン全体の CPU 数を返すため、コンテナに CPU を制限して
    （docker --cpuset-cpus など）動かすと、使えない CPU の数まで QEMU を起動して
    奪い合いになり、タイマーの較正や時間制限のあるケースが不安定になる。
    process_cpu_count()（Python 3.13 以降）は、このプロセスが実際に使える CPU 数を返す。
    """
    count = os.process_cpu_count() if hasattr(os, "process_cpu_count") else os.cpu_count()
    return max(1, count or 1)


def main() -> int:
    parser = argparse.ArgumentParser(description="統合テストを実行する")
    parser.add_argument("--kernel", default="c", choices=sorted(KERNEL_NAMES), help="対象の言語")
    parser.add_argument("--iso", help="起動する ISO（既定: build/<言語>/<名前>.iso）")
    parser.add_argument("--only", action="append", help="このケースだけ実行する（複数指定可）")
    parser.add_argument(
        "--cases-dir",
        type=Path,
        default=ROOT / "tests" / "integration",
        help="テストケース（*.toml）のディレクトリ（既定: tests/integration）",
    )
    kind = parser.add_mutually_exclusive_group()
    kind.add_argument("--ktest", action="store_true", help="テスト用カーネルのケース（kernel = \"ktest\"）だけ実行する")
    kind.add_argument("--no-ktest", action="store_true", help="テスト用カーネルのケースを除く")
    parser.add_argument(
        "-j", "--jobs", type=int, default=default_jobs(), help="同時に起動する QEMU の数（既定: 使える CPU 数）"
    )
    parser.add_argument("--log-dir", type=Path, help="シリアルログの保存先（既定: build/<言語>/test-logs）")
    args = parser.parse_args()

    kernel_name = KERNEL_NAMES[args.kernel]
    out = ROOT / "build" / args.kernel
    iso = Path(args.iso) if args.iso else out / f"{kernel_name}.iso"
    if not iso.exists():
        print(f"ISO がありません: {iso}（先に make iso を実行してください）", file=sys.stderr)
        return 2
    log_dir = args.log_dir or out / "test-logs"
    log_dir.mkdir(parents=True, exist_ok=True)

    try:
        cases = [load_case(p) for p in sorted(args.cases_dir.glob("*.toml"))]
    except ValueError as e:
        print(f"テストケースの読み込みに失敗しました: {e}", file=sys.stderr)
        return 2
    if args.only:
        missing = set(args.only) - {c.name for c in cases}
        if missing:
            print(f"そのようなケースはありません: {', '.join(sorted(missing))}", file=sys.stderr)
            return 2
        cases = [c for c in cases if c.name in args.only]
    if args.ktest:
        cases = [c for c in cases if c.kernel == "ktest"]
    if args.no_ktest:
        cases = [c for c in cases if c.kernel != "ktest"]
    if not cases:
        print("実行するケースがありません", file=sys.stderr)
        return 2

    accel = "KVM" if qemu.kvm_available() else "TCG（KVM なし）"
    print(f"{len(cases)} 件のケースを実行します（{args.kernel} 版、{accel}、同時 {args.jobs}）")
    started = time.monotonic()

    pool = ThreadPoolExecutor(max_workers=max(1, args.jobs))
    results: list[Result] = []
    try:
        futures = [pool.submit(run_case, c, iso, kernel_name, log_dir) for c in cases]
        for future in futures:
            result = future.result()
            results.append(result)
            mark = "PASS" if result.passed else "FAIL"
            print(f"  {mark} {result.case.name} ({result.seconds:.1f} 秒)")
            if not result.passed:
                print(f"       理由: {result.reason}")
                print(f"       ログ: {result.log_path}（末尾 30 行）")
                for line in result.log[-30:]:
                    print(f"       | {line}")
    except KeyboardInterrupt:
        # Ctrl-C: 待っているケースを取り消し、実行中の QEMU を止めてから終わる
        print("\n中断しました。実行中の QEMU を止めます", file=sys.stderr)
        pool.shutdown(wait=False, cancel_futures=True)
        qemu.stop_all()
        return 130
    finally:
        pool.shutdown(wait=True, cancel_futures=True)

    failed = [r for r in results if not r.passed]
    elapsed = time.monotonic() - started
    print(f"結果: {len(results) - len(failed)} 件合格、{len(failed)} 件失敗（{elapsed:.1f} 秒）")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
