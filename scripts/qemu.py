#!/usr/bin/env python3
"""QEMU でカーネルを起動する共通モジュール兼コマンド。

`make run` / `make debug` と統合テストランナー（tests/run.py）の両方がこれを使う。

起動の仕組み:
  - UEFI ファームウェア OVMF で起動し、ISO 上の Limine がカーネルを読み込む。
  - 起動するカーネルやコマンドラインを差し替えたいときは、ISO を作り直さずに、
    Limine の「SMBIOS Type 11 OEM String による設定」を使う。QEMU の
    `-smbios type=11,path=<ファイル>` で `limine:config:` から始まる文字列を渡すと、
    Limine は ISO 上の limine.conf より先にその設定を採用する。
  - この方法では設定ファイルが ISO 上にないので、`boot():`（設定ファイルのあるパーティション）
    が使えない。そこで「1 台目の光学ドライブ全体」を表す `odd(1:):/...` でファイルを指定する。
    QEMU には -cdrom の 1 台しか光学ドライブを付けないので、これで必ず ISO を指す。
    （ISO9660 のボリュームラベルを使う fslabel() は Limine 11.2.1 では使えなかった）
  - isa-debug-exit デバイスを付けておくと、カーネルが I/O ポート 0xf4 に書いた値 v に
    応じて QEMU が終了コード (v << 1) | 1 で終了する（テストの合否の伝達に使う）。

コマンドとしての使い方:
  python3 scripts/qemu.py --iso build/c/kui.iso
  python3 scripts/qemu.py --iso build/c/kui.iso --cmdline "selftest=panic" --timeout 10
  python3 scripts/qemu.py --iso build/c/kui.iso --gdb
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

# SMBIOS 経由の設定で ISO 上のファイルを指すときの接頭辞（上の説明を参照）
ISO_RESOURCE = "odd(1:):"

# isa-debug-exit による QEMU の終了コード（カーネル側 kui/qemu.h と対応）
EXIT_SUCCESS = 33  # カーネルが 0x10 を書いた
EXIT_FAILURE = 35  # カーネルが 0x11 を書いた

DEFAULT_OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
DEFAULT_OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"


def kvm_available() -> bool:
    """KVM（ハードウェア仮想化）が使えるか。TANGMEN_NO_KVM=1 で強制的に無効化できる。"""
    if os.environ.get("TANGMEN_NO_KVM", "") not in ("", "0"):
        return False
    return os.access("/dev/kvm", os.R_OK | os.W_OK)


def _check_config_value(what: str, value: str) -> None:
    """Limine の設定に埋め込む値を検査する。

    設定ファイルは 1 行 1 項目なので、値に改行が入ると別の項目を注入できてしまう。
    また SMBIOS の文字列は NUL 終端なので、NUL もそこで切れてしまう。どちらも拒否する。
    """
    if any(c in value for c in "\r\n\0"):
        raise ValueError(f"{what} に改行や NUL は使えません: {value!r}")


def limine_config(kernel_path: str, cmdline: str | None, title: str = "Kui") -> str:
    """Limine の設定ファイルの中身を作る（書式は boot/limine.conf と同じ）。"""
    _check_config_value("カーネルのパス", kernel_path)
    if cmdline:
        _check_config_value("コマンドライン", cmdline)
    lines = ["timeout: 0", "", f"/{title}", "    protocol: limine", f"    path: {kernel_path}"]
    if cmdline:
        lines.append(f"    cmdline: {cmdline}")
    return "\n".join(lines) + "\n"


def build_command(
    iso: str | os.PathLike,
    workdir: str | os.PathLike,
    *,
    kernel_path: str | None = None,
    cmdline: str | None = None,
    gdb: bool = False,
    display: bool = False,
    serial: str = "stdio",
    memory: str = "256M",
) -> list[str]:
    """QEMU のコマンドラインを組み立てる。

    workdir には一時ファイル（OVMF の変数領域のコピー、SMBIOS 用の設定）を置く。
    kernel_path（ISO 内のパス。例: /boot/kui-ktest.elf）か cmdline を指定すると、
    SMBIOS 経由で Limine の設定を差し替える。
    """
    workdir = Path(workdir)
    ovmf_code = os.environ.get("OVMF_CODE", DEFAULT_OVMF_CODE)
    ovmf_vars = os.environ.get("OVMF_VARS", DEFAULT_OVMF_VARS)

    # OVMF の変数領域は QEMU が書き込むので、実行ごとにコピーを使う
    vars_copy = workdir / "OVMF_VARS.fd"
    shutil.copyfile(ovmf_vars, vars_copy)

    if kvm_available():
        accel = ["-accel", "kvm", "-cpu", "host"]
    else:
        accel = ["-accel", "tcg", "-cpu", "max"]

    cmd = [
        "qemu-system-x86_64",
        "-machine", "q35",
        "-m", memory,
        *accel,
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={vars_copy}",
        "-cdrom", str(iso),
        "-no-reboot",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-serial", serial,
    ]
    if not display:
        cmd += ["-display", "none"]

    if kernel_path is not None or cmdline is not None:
        config = limine_config(ISO_RESOURCE + (kernel_path or "/boot/kui.elf"), cmdline)
        smbios_file = workdir / "limine-smbios.conf"
        smbios_file.write_text("limine:config:" + config)
        # QEMU のオプション値ではカンマを ",," と書く必要がある
        cmd += ["-smbios", "type=11,path=" + str(smbios_file).replace(",", ",,")]

    if gdb:
        # -s: TCP 1234 で GDB の接続を待ち受ける / -S: 接続されるまで CPU を止めておく
        cmd += ["-s", "-S"]
    return cmd


# 実行中の QemuRun（Ctrl-C などで中断したときに、まとめて止めるため）
_active_runs: set[QemuRun] = set()
_active_lock = threading.Lock()


def stop_all() -> None:
    """実行中の QEMU をすべて止める（テストランナーの中断時に使う）。"""
    with _active_lock:
        runs = list(_active_runs)
    for run in runs:
        run.stop()


class QemuRun:
    """QEMU を起動し、シリアル出力を 1 行ずつ受け取れるようにするクラス。

    with 文で使うと、終了時に QEMU を確実に止めて一時ファイルを消す。
    """

    def __init__(
        self,
        iso: str | os.PathLike,
        *,
        kernel_path: str | None = None,
        cmdline: str | None = None,
        memory: str = "256M",
    ) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="tangmen-qemu-")
        try:
            self.command = build_command(
                iso, self._tmp.name, kernel_path=kernel_path, cmdline=cmdline, memory=memory
            )
            self.proc = subprocess.Popen(
                self.command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        except BaseException:
            # 起動に失敗したら一時ディレクトリを残さない
            self._tmp.cleanup()
            raise
        with _active_lock:
            _active_runs.add(self)

    def readline(self) -> str | None:
        """シリアル出力を 1 行読む。QEMU が終了したら None。"""
        assert self.proc.stdout is not None
        raw = self.proc.stdout.readline()
        if not raw:
            return None
        return raw.decode("utf-8", errors="replace").rstrip("\r\n").replace("\r", "")

    def stop(self) -> None:
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()

    def close(self) -> None:
        self.stop()
        with _active_lock:
            _active_runs.discard(self)
        if self.proc.stdout:
            self.proc.stdout.close()
        self._tmp.cleanup()

    def __enter__(self) -> QemuRun:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="QEMU でカーネルを起動する")
    parser.add_argument("--iso", required=True, help="起動する ISO イメージ")
    parser.add_argument("--kernel-path", help="起動するカーネルの ISO 内のパス（例: /boot/kui-ktest.elf）")
    parser.add_argument("--cmdline", help="カーネルのコマンドライン")
    parser.add_argument("--gdb", action="store_true", help="GDB の接続を待ってから実行する")
    parser.add_argument(
        "--symbols", default="build/c/kui.elf", help="--gdb のときに案内する、シンボル付きの ELF"
    )
    parser.add_argument("--display", action="store_true", help="画面のウィンドウを表示する")
    parser.add_argument("--timeout", type=float, help="指定秒数で QEMU を止める")
    parser.add_argument("--serial-log", help="シリアル出力をこのファイルにも保存する")
    args = parser.parse_args()
    if not Path(args.iso).is_file():
        print(f"[qemu.py] ISO がありません: {args.iso}", file=sys.stderr)
        return 2

    accel = "KVM" if kvm_available() else "TCG（KVM なし。遅い）"
    print(f"[qemu.py] 高速化: {accel}", file=sys.stderr)

    with tempfile.TemporaryDirectory(prefix="tangmen-qemu-") as tmp:
        interactive = args.timeout is None and args.serial_log is None
        cmd = build_command(
            args.iso,
            tmp,
            kernel_path=args.kernel_path,
            cmdline=args.cmdline,
            gdb=args.gdb,
            display=args.display,
            # 対話時は "mon:stdio" にして、Ctrl-A X で QEMU を終了できるようにする
            serial="mon:stdio" if interactive else "stdio",
        )
        if args.gdb:
            print(
                "[qemu.py] GDB の接続を待っています（TCP 1234）。別の端末で次を実行してください:\n"
                f"  scripts/dev gdb {args.symbols} -ex 'target remote localhost:1234'\n"
                "  ブレークポイントは hbreak（ハードウェアブレークポイント）を使うこと。\n"
                "  起動直後はまだカーネルが読み込まれておらず、break（メモリに int3 を書き込む方式）\n"
                "  ではカーネルのアドレスに書き込めないため。例:\n"
                "    (gdb) hbreak kmain\n"
                "    (gdb) continue",
                file=sys.stderr,
            )
        if interactive:
            print("[qemu.py] 終了するには Ctrl-A を押してから X を押してください", file=sys.stderr)
            return subprocess.run(cmd).returncode

        log = open(args.serial_log, "wb") if args.serial_log else None
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        def pump() -> None:
            assert proc.stdout is not None
            for chunk in iter(lambda: proc.stdout.read1(4096), b""):
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                if log:
                    log.write(chunk)

        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        try:
            code = proc.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print(f"\n[qemu.py] {args.timeout} 秒経ったので QEMU を止めました", file=sys.stderr)
            proc.kill()
            proc.wait()
            code = 0
        reader.join(timeout=2)
        if proc.stdout:
            proc.stdout.close()
        if log:
            log.close()
        return code


if __name__ == "__main__":
    sys.exit(main())
