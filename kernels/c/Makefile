# Kui（Tangmen の C 版カーネル）のビルド
#
#   make                  通常のカーネル（kui.elf）とテスト用カーネル（kui-ktest.elf）を作る
#   make BUILD=release    UBSan なしでビルドする
#   make test-host        ホスト上の単体テストを実行する
#   make format           ソースを clang-format で整形する
#   make format-check     整形済みかを確認する（CI 用）
#
# 出力先は OUT（既定: リポジトリ直下の build/c）。BUILD ごとに中間ファイルを分ける。

BUILD ?= debug
OUT   ?= ../../build/c
# 相対パスで渡されても、サブ make（tests/host）や出力先がずれないよう絶対パスにする
override OUT := $(abspath $(OUT))

ifeq ($(filter $(BUILD),debug release),)
$(error BUILD must be debug or release (got '$(BUILD)'))
endif

CC          := clang
LD          := ld.lld
CLANG_FORMAT ?= clang-format

OBJDIR       := $(OUT)/obj/$(BUILD)
OBJDIR_KTEST := $(OUT)/obj/$(BUILD)-ktest
KERNEL       := $(OUT)/kui.elf
KERNEL_KTEST := $(OUT)/kui-ktest.elf

# カーネル用の共通フラグ
#   --target ... -ffreestanding : OS のない環境向け（標準ライブラリなし）
#   -mcmodel=kernel             : 最上位 2GiB に置かれるコードを生成
#   -mno-red-zone               : 割り込みがスタックの「レッドゾーン」を壊さないように
#   -mno-sse ... -mno-80387     : 浮動小数点・SIMD レジスタを使わない（Limine は無効のまま渡す）
#   -fno-omit-frame-pointer     : スタックトレースを辿れるようにする
#   -fno-*unwind-tables         : C++ の例外処理用の .eh_frame を作らない（デバッグ情報は -g で別に出る）
#   -Wstrict-prototypes ...     : 引数の型を書かない古い関数宣言や、宣言のない外部関数を禁止する
CFLAGS_COMMON := --target=x86_64-unknown-none-elf -std=c17 -ffreestanding \
	-fno-stack-protector -fno-stack-check -fno-PIC -fno-pie -mcmodel=kernel \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-80387 -fno-omit-frame-pointer \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-Wall -Wextra -Werror -Wstrict-prototypes -Wmissing-prototypes \
	-MMD -MP -Iinclude -Ithird_party/limine -O2 -g

# debug ビルドでは UBSan を有効にする（ハンドラは src/ubsan.c）
ifeq ($(BUILD),debug)
SANITIZE := -fsanitize=undefined
else
SANITIZE :=
endif

CFLAGS := $(CFLAGS_COMMON) $(SANITIZE)

# 検査や builtin 化を外すファイル
#   string.c : memcpy 等の実装。コンパイラに memcpy 呼び出しへ置き換えられると再帰する
#   ubsan.c  : UBSan のハンドラ自身。検査に引っかかると無限に再帰する
NOSAN_SRCS  := src/lib/string.c src/ubsan.c
NOSAN_FLAGS := -fno-sanitize=all -fno-builtin

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -z noexecstack -T linker.ld

SRCS := $(sort $(wildcard src/*.c src/*/*.c src/*/*/*.c))
# ktest.c はテスト用カーネルにだけ入れる
KERNEL_SRCS := $(filter-out src/ktest.c,$(SRCS))
KTEST_SRCS  := $(SRCS) $(sort $(wildcard tests/ktest/*.c))

KERNEL_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(KERNEL_SRCS))
KTEST_OBJS  := $(patsubst %.c,$(OBJDIR_KTEST)/%.o,$(KTEST_SRCS))

FORMAT_FILES := $(sort $(wildcard src/*.c src/*/*.c src/*/*/*.c include/kui/*.h \
	include/kui/*/*.h include/kui/*/*/*.h tests/*/*.c tests/*/*.h))

.DEFAULT_GOAL := all

.PHONY: all test-host format format-check clean FORCE

# BUILD を切り替えたときに古い種類のオブジェクトでリンク済みの ELF が残らないよう、
# リンクは毎回やり直す（数十ミリ秒で終わる）
FORCE:

all: $(KERNEL) $(KERNEL_KTEST)

$(KERNEL): $(KERNEL_OBJS) linker.ld FORCE
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_KTEST): $(KTEST_OBJS) linker.ld FORCE
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(KTEST_OBJS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(if $(filter $<,$(NOSAN_SRCS)),$(NOSAN_FLAGS)) -c $< -o $@

$(OBJDIR_KTEST)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DKUI_KTEST $(if $(filter $<,$(NOSAN_SRCS)),$(NOSAN_FLAGS)) -c $< -o $@

test-host:
	$(MAKE) -C tests/host test OUT=$(OUT)/host

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	$(CLANG_FORMAT) --dry-run -Werror $(FORMAT_FILES)

clean:
	rm -rf $(OUT)

-include $(KERNEL_OBJS:.o=.d) $(KTEST_OBJS:.o=.d)
