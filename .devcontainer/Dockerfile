# Tangmen の開発環境。ローカル開発（Dev Container / scripts/dev）と CI の両方でこのイメージを使う。
FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive

# ビルド・実行・デバッグに使うツール一式
#   clang / lld / llvm : カーネルのクロスビルド（--target=x86_64-elf）とホスト単体テスト
#   libclang-rt-21-dev : ホスト単体テストで使う AddressSanitizer / UBSan のランタイム
#   qemu-system-x86 / ovmf : UEFI で起動する QEMU
#   xorriso : 起動用 ISO イメージの作成
#   gdb : QEMU に接続してカーネルをデバッグ
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates git make python3 \
      clang lld llvm clang-format libclang-rt-21-dev \
      qemu-system-x86 ovmf \
      xorriso gdb \
 && rm -rf /var/lib/apt/lists/*

# Limine（ブートローダー）の配布バイナリ。バージョンを固定して取得する。
# 更新するときは docs/third-party.md も合わせて更新すること。
ARG LIMINE_VERSION=v11.2.1
RUN git clone --depth 1 --branch ${LIMINE_VERSION}-binary \
      https://codeberg.org/Limine/Limine.git /opt/limine \
 && rm -rf /opt/limine/.git

ENV LIMINE_DIR=/opt/limine \
    OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd \
    OVMF_VARS=/usr/share/OVMF/OVMF_VARS_4M.fd
