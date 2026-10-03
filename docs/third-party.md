# 外部ソフトウェアとライセンス

このリポジトリは MIT ライセンスです。MIT と両立しないライセンス（GPL など）のコードやバイナリは
リポジトリに含めません。外部ソフトはバージョンを固定し、ビルド時に取得します。

外部ソフトを追加・更新したときは、この表も更新してください。

## 現在使っているもの

| ソフト | バージョン | ライセンス | 用途 | 入手方法 | リポジトリへの同梱 |
| --- | --- | --- | --- | --- | --- |
| [Limine](https://codeberg.org/Limine/Limine)（バイナリ配布） | v11.2.1（タグ `v11.2.1-binary`） | BSD-2-Clause | ブートローダー（UEFI 起動） | `.devcontainer/Dockerfile` で取得し、`/opt/limine` に置く | なし |
| [limine.h](https://github.com/limine-bootloader/limine-protocol)（Limine ブートプロトコルのヘッダ） | commit `3a0526b700e356f0eac1b71a77697b3fd1c707a3` | 0BSD 相当（著作権表示の保持も不要な許諾） | カーネルが Limine に情報を要求するための定義 | 手動でコピー | あり（`kernels/c/third_party/limine/`、`LICENSE` と `VERSION` を同梱） |
| [OVMF](https://github.com/tianocore/edk2)（UEFI ファームウェア） | Ubuntu 26.04 の `ovmf` パッケージ（2025.11） | BSD-2-Clause-Patent | QEMU で UEFI 起動するため | Ubuntu パッケージ | なし |
| [QEMU](https://www.qemu.org/) | Ubuntu 26.04 の `qemu-system-x86` パッケージ（10.2） | GPLv2 | エミュレータ。ツールとして実行するだけ | Ubuntu パッケージ | なし |
| [clang / LLVM / lld](https://llvm.org/)（compiler-rt を含む） | Ubuntu 26.04 のパッケージ（21.1） | Apache-2.0 WITH LLVM-exception | コンパイラ、リンカ、clang-format、ホスト単体テスト用のサニタイザのランタイム（`libclang-rt-21-dev`）。ツールとして実行するだけで、カーネルにはリンクしない | Ubuntu パッケージ | なし |
| [Python](https://www.python.org/) | Ubuntu 26.04 のパッケージ（3.14） | PSF-2.0 | QEMU の起動スクリプトとテストランナー（`scripts/qemu.py`、`tests/run.py`）の実行 | Ubuntu パッケージ | なし |
| [GDB](https://www.sourceware.org/gdb/) | Ubuntu 26.04 のパッケージ（17.1） | GPLv3 | デバッガ。ツールとして実行するだけ | Ubuntu パッケージ | なし |
| [xorriso](https://www.gnu.org/software/xorriso/) | Ubuntu 26.04 のパッケージ（1.5.6） | GPLv3 | 起動用 ISO イメージの作成。ツールとして実行するだけ | Ubuntu パッケージ | なし |

GPL のツール（QEMU、GDB、xorriso）は、開発環境の中で実行するだけです。カーネルやリポジトリのコードには
組み込まないので、リポジトリのライセンスには影響しません。

ビルドで作られる起動用 ISO（`build/<言語>/*.iso`）には、Limine のバイナリ（`BOOTX64.EFI`、`limine-uefi-cd.bin`）が入ります。
ISO はリポジトリにも CI の成果物にも含めていませんが、ISO を配布する場合は、BSD-2-Clause の条件に従って
Limine の著作権表示とライセンス文（`/opt/limine/LICENSE`）を添付してください。

## 将来導入する予定のもの

| ソフト | ライセンス | 導入時期 | 方針 |
| --- | --- | --- | --- |
| [musl libc](https://musl.libc.org/) | MIT | M5（ユーザープログラムの実行） | バージョンを固定し、ビルドスクリプトで取得してビルドする。バイナリは同梱しない |
| [BusyBox](https://busybox.net/) | GPLv2 | M8 | バイナリを配布するとソース提供義務が生じるため、リポジトリには置かない。ビルドスクリプトで毎回取得してビルドする |
| 画面コンソール用のビットマップフォント | 未定（パブリックドメインか MIT 相当から選ぶ） | M6 | ライセンスを確認してから同梱する |

## 参照のみ（コードは使わない）

| 対象 | 扱い |
| --- | --- |
| Linux（GPLv2） | システムコールの仕様は man ページや仕様書で確認する。ソースコードは読まないし、コピーもしない |
| その他の既存 OS（xv6 など） | ソースコードは読まない（AI 能力検証の条件） |
