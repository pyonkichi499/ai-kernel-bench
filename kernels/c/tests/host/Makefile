# ホスト単体テスト
#
# ハードウェアに依存しないカーネルのコード（src/lib/）を、普通の Linux プログラムとして
# ビルドし、AddressSanitizer と UBSan を有効にして実行する。
#
#   make test          ビルドして実行
#   make test OUT=...  出力先を変える（絶対パス可）
#   make clean

OUT ?= ../../../../build/c/host
override OUT := $(abspath $(OUT))

HOST_CC := clang
CFLAGS := -std=c17 -Wall -Wextra -Werror -g -O1 \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-DKUI_HOST_TEST -I../../include -MMD -MP
LDFLAGS := -fsanitize=address,undefined

LIB_SRCS := $(wildcard ../../src/lib/*.c)
TEST_SRCS := $(wildcard *.c)

LIB_OBJS := $(patsubst ../../src/lib/%.c,$(OUT)/lib/%.o,$(LIB_SRCS))
TEST_OBJS := $(patsubst %.c,$(OUT)/%.o,$(TEST_SRCS))
BIN := $(OUT)/host-tests

.PHONY: test clean

test: $(BIN)
	$(BIN)

$(BIN): $(LIB_OBJS) $(TEST_OBJS)
	$(HOST_CC) $(LDFLAGS) -o $@ $^

# string.c は自分自身の呼び出しへの置き換えを防ぐため -fno-builtin でビルドする
$(OUT)/lib/string.o: CFLAGS += -fno-builtin

$(OUT)/lib/%.o: ../../src/lib/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OUT)

-include $(LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d)
