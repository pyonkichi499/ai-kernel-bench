# Tangmen のルート Makefile。
#
# 各言語のカーネル（kernels/<言語>/）のビルドを呼び出し、起動用 ISO の作成、
# QEMU での起動、テストの実行をまとめて行う。開発イメージの中で実行すること
# （ホストからは scripts/dev make <ターゲット>）。
#
# 主な変数:
#   KERNEL  対象の言語（c）。既定は c
#   BUILD   debug（UBSan 有効）または release。既定は debug
#           切り替えたときは make clean してからビルドすること（ci は release を別ディレクトリに出す）
#   CMDLINE run / debug 時のカーネルのコマンドライン
#   DISPLAY_QEMU=1  run / debug で QEMU の画面ウィンドウを表示する
#   JOBS    統合テストを同時に走らせる数（既定は CPU 数）

KERNEL ?= c
BUILD ?= debug
CMDLINE ?=
DISPLAY_QEMU ?=
JOBS ?=

# 言語ごとのカーネル名
NAME_c := kui
NAME := $(NAME_$(KERNEL))
ifeq ($(NAME),)
$(error 未知の KERNEL=$(KERNEL) です（対応している言語: c）)
endif

LIMINE_DIR ?= /opt/limine
PYTHON ?= python3

ROOT := $(abspath .)
OUT := $(ROOT)/build/$(KERNEL)
KERNEL_DIR := $(ROOT)/kernels/$(KERNEL)
ISO := $(OUT)/$(NAME).iso
ISO_ROOT := $(OUT)/iso_root
ELF := $(OUT)/$(NAME).elf
KTEST_ELF := $(OUT)/$(NAME)-ktest.elf

# CMDLINE はシェルの単一引用符で囲んで渡す（中の ' は '\'' に置き換える）。
# こうすると空白や " $ などを含むコマンドラインもそのままカーネルに届く。
QUOTED_CMDLINE := '$(subst ','\'',$(CMDLINE))'
QEMU_ARGS := --iso $(ISO) --symbols build/$(KERNEL)/$(NAME).elf \
	$(if $(CMDLINE),--kernel-path /boot/$(NAME).elf --cmdline $(QUOTED_CMDLINE)) \
	$(if $(DISPLAY_QEMU),--display)
RUN_TESTS := $(PYTHON) $(ROOT)/tests/run.py --kernel $(KERNEL) --iso $(ISO) $(if $(JOBS),-j $(JOBS))

.PHONY: all build iso run debug test test-host test-ktest test-integration \
	format format-check ci clean help

# このファイルのターゲットは並列に実行しない。test-ktest と test-integration が
# 同時に iso を作り直すと、同じ ISO を同時に書き換えて壊してしまうため。
# （各言語の Makefile の中での並列ビルドには影響しない）
.NOTPARALLEL:

all: iso

help:
	@echo "使い方: make <ターゲット> [KERNEL=c] [BUILD=debug|release] [CMDLINE=...]"
	@echo ""
	@echo "  build             カーネルをビルドする"
	@echo "  iso               起動用 ISO（$(ISO)）を作る"
	@echo "  run               QEMU で起動する（Ctrl-A X で終了）"
	@echo "  debug             QEMU を GDB 待ち受け状態で起動する"
	@echo "  test              テストを全部実行する（test-host test-ktest test-integration）"
	@echo "  test-host         ホスト上の単体テスト"
	@echo "  test-ktest        カーネル内の単体テスト（QEMU）"
	@echo "  test-integration  統合テスト（QEMU）"
	@echo "  format            ソースを clang-format で整形する"
	@echo "  format-check      整形されているか確認する"
	@echo "  ci                CI と同じ検査（format-check、release ビルド、debug で全テスト）"
	@echo "  clean             ビルド結果を消す"
	@echo ""
	@echo "変数:"
	@echo "  KERNEL=c                  対象の言語"
	@echo "  BUILD=debug|release       debug は UBSan 有効（切り替えたら make clean）"
	@echo "  CMDLINE='...'             run / debug 時のカーネルのコマンドライン"
	@echo "  DISPLAY_QEMU=1            run / debug で QEMU の画面を表示する"
	@echo "  JOBS=N                    統合テストを同時に走らせる数"
	@echo "  TANGMEN_NO_KVM=1          KVM を使わない（環境変数）"

# カーネル本体のビルドは各言語の Makefile に任せる。
# 依存関係の判断も向こうに任せるため、毎回呼び出す（変更がなければ何もしない）。
build:
	$(MAKE) -C $(KERNEL_DIR) all BUILD=$(BUILD) OUT=$(OUT)

# 起動用 ISO（UEFI のみ）を作る。ISO にはテスト用カーネルも入れておき、
# テストでは SMBIOS 経由の Limine 設定でどちらを起動するか選ぶ。
iso: build
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(ELF) $(KTEST_ELF) $(ISO_ROOT)/boot/
	cp $(ROOT)/boot/limine.conf $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO) 2>$(OUT)/xorriso.log \
		|| { cat $(OUT)/xorriso.log; exit 1; }
	@echo "ISO を作りました: $(ISO)"

run: iso
	$(PYTHON) $(ROOT)/scripts/qemu.py $(QEMU_ARGS)

debug: iso
	$(PYTHON) $(ROOT)/scripts/qemu.py $(QEMU_ARGS) --gdb

test: test-host test-ktest test-integration

test-host:
	$(MAKE) -C $(KERNEL_DIR) test-host BUILD=$(BUILD) OUT=$(OUT)

test-ktest: iso
	$(RUN_TESTS) --ktest

test-integration: iso
	$(RUN_TESTS) --no-ktest

format:
	$(MAKE) -C $(KERNEL_DIR) format

format-check:
	$(MAKE) -C $(KERNEL_DIR) format-check

# CI と同じ検査。release でビルドが通ることを確認してから、debug（UBSan 有効）で全テストを行う。
# release のビルド結果は debug と混ざらないよう別のディレクトリに出す。
ci: format-check
	$(MAKE) -C $(KERNEL_DIR) all BUILD=release OUT=$(OUT)-release
	$(MAKE) test BUILD=debug

clean:
	-$(MAKE) -C $(KERNEL_DIR) clean OUT=$(OUT)
	rm -rf $(OUT) $(OUT)-release
