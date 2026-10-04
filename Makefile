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

# ---------------------------------------------------------------------------
# 出力先（build/<言語>）のロック
#
# 複数の make（別の端末、並列に動くエージェントなど）が同じ build/<言語> を同時に
# 書き換えると、2 段階リンクの途中のファイルや ISO を壊し合う。そこで、make を
# 呼ぶと、まず flock でロックを取ってから自分自身を TANGMEN_LOCKED=1 付きで実行し直す。
# 2 回目の make（と、そこから呼ばれる make）はロックを持っているので、そのまま進む。
#
# run / debug だけは、ISO を作るところまでロックを持ち、QEMU はロックの外で動かす。
# QEMU を動かしている間ずっと、別の端末のテストを待たせないようにするため。
# ---------------------------------------------------------------------------
LOCK_FILE := $(ROOT)/build/.lock-$(KERNEL)
# ロックが取れなければメッセージを出してから待つ（最大 30 分）
WITH_LOCK = mkdir -p $(ROOT)/build && \
	{ flock -n $(LOCK_FILE) true || echo "make: $(LOCK_FILE) を他の make が使用中のため待っています..." >&2; } && \
	TANGMEN_LOCKED=1 flock -w 1800 $(LOCK_FILE)

ifeq ($(TANGMEN_LOCKED),)

GOALS := $(or $(MAKECMDGOALS),all)
QEMU_GOALS := $(filter run debug,$(GOALS))
OTHER_GOALS := $(filter-out run debug,$(GOALS))

.PHONY: $(GOALS) locked-goals
.NOTPARALLEL:

# 指定されたターゲットは全部、ロックを取った再実行（locked-goals）に任せる
$(OTHER_GOALS): locked-goals
	@:

locked-goals:
	@$(if $(OTHER_GOALS),$(WITH_LOCK) $(MAKE) --no-print-directory $(OTHER_GOALS),:)

$(QEMU_GOALS): locked-goals
	@$(WITH_LOCK) $(MAKE) --no-print-directory iso
	@TANGMEN_LOCKED=1 $(MAKE) --no-print-directory $@-qemu

else

.PHONY: all build iso run debug run-qemu debug-qemu test test-host test-ktest test-integration \
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
		$(ISO_ROOT) -o $(ISO).tmp 2>$(OUT)/xorriso.log \
		|| { cat $(OUT)/xorriso.log; exit 1; }
	@# 一時ファイルに作ってから置き換える。実行中の QEMU（make run）が開いている
	@# 古い ISO は、置き換えても中身が変わらない（別のファイルとして残る）
	mv -f $(ISO).tmp $(ISO)
	@echo "ISO を作りました: $(ISO)"

run: iso run-qemu
debug: iso debug-qemu

# QEMU の起動だけを行う（ISO は作らない）。ロックの外で動かすために分けている
run-qemu:
	$(PYTHON) $(ROOT)/scripts/qemu.py $(QEMU_ARGS)

debug-qemu:
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

endif
