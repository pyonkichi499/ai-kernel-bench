#!/usr/bin/env python3
"""カーネルの ELF から関数のシンボル表を作り、C のソースとして標準出力へ書く。

スタックトレースで戻りアドレスを関数名に変換するため、カーネル自身に
「関数の開始アドレス・大きさ・名前」の表を埋め込む。手順（kernels/c/Makefile）:

  1. 件数 0 の仮の表（tools/symtab_stub.c）を入れて 1 回目のリンクをする
  2. 1 回目の ELF からこのスクリプトで表を生成し、それを入れて 2 回目のリンクをする
  3. 2 回目の ELF からもう一度表を生成し、2. と完全に一致することを確認する

表はデータ（.rodata）にしか置かれず、コード（.text）の大きさや並びは変わらないので、
関数のアドレスは 1 回目と 2 回目で同じになる。3. の確認はその前提が崩れていないことを
機械的に保証するためのもので、一致しなければビルドを失敗させる。

使い方: gen_symtab.py <llvm-nm> <kernel.elf>
       gen_symtab.py --stub   （件数 0 の仮の表を出力）
"""

import subprocess
import sys

# nm の種類のうち、関数（コード）を表すもの。t/T: ローカル/グローバル、w/W: 弱いシンボル
CODE_TYPES = {"t", "T", "w", "W"}


def read_symbols(nm: str, elf: str) -> list[tuple[int, int, str]]:
    out = subprocess.run(
        [nm, "-n", "-S", "--defined-only", elf],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    symbols: dict[int, tuple[int, str]] = {}
    for line in out.splitlines():
        parts = line.split()
        # 大きさあり: "<addr> <size> <type> <name>"、なし: "<addr> <type> <name>"
        if len(parts) == 4:
            addr, size, kind, name = int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]
        elif len(parts) == 3:
            addr, size, kind, name = int(parts[0], 16), 0, parts[1], parts[2]
        else:
            continue
        if kind not in CODE_TYPES:
            continue
        # アセンブラの一時ラベルなどは除く
        if name.startswith((".L", "$")):
            continue
        # 同じアドレスに複数の名前があれば、大きさの分かる方（なければ先の方）を残す
        if addr in symbols and (symbols[addr][0] != 0 or size == 0):
            continue
        symbols[addr] = (size, name)
    return [(addr, size, name) for addr, (size, name) in sorted(symbols.items())]


def emit(symbols: list[tuple[int, int, str]]) -> str:
    lines = [
        "/* 自動生成ファイル（kernels/c/tools/gen_symtab.py）。編集しないこと */",
        "#include <stdint.h>",
        "",
        f"const uint64_t kui_symtab_count = {len(symbols)};",
    ]
    # 件数 0 でも配列の定義が必要なので、少なくとも 1 要素の大きさを取る
    n = max(len(symbols), 1)
    lines.append(f"const uint64_t kui_symtab_addrs[{n}] = {{")
    lines += [f"\t0x{addr:016x}ULL," for addr, _, _ in symbols] or ["\t0,"]
    lines.append("};")
    lines.append(f"const uint64_t kui_symtab_sizes[{n}] = {{")
    lines += [f"\t0x{size:x}ULL," for _, size, _ in symbols] or ["\t0,"]
    lines.append("};")
    lines.append(f"const char *const kui_symtab_names[{n}] = {{")
    lines += [f'\t"{name}",' for _, _, name in symbols] or ["\t0,"]
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--stub":
        sys.stdout.write(emit([]))
        return 0
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    sys.stdout.write(emit(read_symbols(sys.argv[1], sys.argv[2])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
