/*
 * ライブラリ（string / format / cmdline）がカーネル環境でも正しく動くことの確認。
 *
 * 同じ関数はホスト上の単体テストでも詳しく検証している。ここでは、
 * freestanding・SSE なし・-mcmodel=kernel という本番と同じ条件でビルドしたものが
 * 期待どおり動くこと（コンパイラの最適化や呼び出し規約の違いで壊れないこと）を確かめる。
 */
#include <stdint.h>

#include <kui/cmdline.h>
#include <kui/format.h>
#include <kui/ktest.h>
#include <kui/string.h>

KTEST(string_memcpy_and_memcmp)
{
	char src[16] = "hello, kernel";
	char dst[16];

	memset(dst, 0, sizeof(dst));
	memcpy(dst, src, sizeof(src));
	KEXPECT_EQ(memcmp(dst, src, sizeof(src)), 0);
	KEXPECT_STREQ(dst, "hello, kernel");
	dst[0] = 'H';
	KEXPECT(memcmp(dst, src, sizeof(src)) < 0);
}

KTEST(string_memmove_overlapping)
{
	char buf[16] = "0123456789";

	/* 後ろへずらす（コピー先がコピー元と重なる） */
	memmove(buf + 2, buf, 8);
	KEXPECT_STREQ(buf, "0101234567");

	/* 前へずらす */
	memcpy(buf, "0123456789", 11);
	memmove(buf, buf + 2, 9);
	KEXPECT_STREQ(buf, "23456789");
}

KTEST(string_memset_large)
{
	/* 大きめの領域で、コンパイラが memset 呼び出しに置き換えても正しく動くこと */
	static unsigned char buf[4096];

	memset(buf, 0xA5, sizeof(buf));
	for (size_t i = 0; i < sizeof(buf); i++) {
		if (buf[i] != 0xA5) {
			KEXPECT_EQ(buf[i], 0xA5);
			break;
		}
	}
}

KTEST(string_str_functions)
{
	char small[4];

	KEXPECT_EQ(strlen(""), 0);
	KEXPECT_EQ(strlen("kui"), 3);
	KEXPECT_EQ(strnlen("kui", 2), 2);
	KEXPECT(strcmp("abc", "abd") < 0);
	KEXPECT(strcmp("abd", "abc") > 0);
	KEXPECT_EQ(strcmp("same", "same"), 0);
	KEXPECT_EQ(strncmp("abcX", "abcY", 3), 0);
	KEXPECT_EQ(strlcpy(small, "truncated", sizeof(small)), 9);
	KEXPECT_STREQ(small, "tru");
}

KTEST(format_basic_conversions)
{
	char buf[128];

	ksnprintf(buf, sizeof(buf), "%d %i %u", -42, 7, 3000000000u);
	KEXPECT_STREQ(buf, "-42 7 3000000000");
	ksnprintf(buf, sizeof(buf), "%x %X %c %s %%", 0xbeefu, 0xbeefu, 'k', "ui");
	KEXPECT_STREQ(buf, "beef BEEF k ui %");
	ksnprintf(buf, sizeof(buf), "[%5d|%-5d|%05d]", 42, 42, 42);
	KEXPECT_STREQ(buf, "[   42|42   |00042]");
}

KTEST(format_64bit_values)
{
	char buf[64];

	/* カーネルのアドレスは 64 ビットいっぱいを使う。上位ビットが落ちないこと */
	ksnprintf(buf, sizeof(buf), "0x%llx", 0xffffffff80000000ull);
	KEXPECT_STREQ(buf, "0xffffffff80000000");
	ksnprintf(buf, sizeof(buf), "%lld", (long long)INT64_MIN);
	KEXPECT_STREQ(buf, "-9223372036854775808");
	ksnprintf(buf, sizeof(buf), "%zu", (size_t)18446744073709551615ull);
	KEXPECT_STREQ(buf, "18446744073709551615");
}

KTEST(format_truncation)
{
	char buf[8];
	int n = ksnprintf(buf, sizeof(buf), "%s", "0123456789");

	KEXPECT_EQ(n, 10);
	KEXPECT_STREQ(buf, "0123456");
}

KTEST(cmdline_parsing)
{
	const char *cmdline = "quiet selftest=ubsan  loglevel=debug";
	char value[16];

	KEXPECT(cmdline_has(cmdline, "quiet"));
	KEXPECT(cmdline_has(cmdline, "selftest"));
	KEXPECT(!cmdline_has(cmdline, "self"));
	KEXPECT(!cmdline_has("", "quiet"));
	KEXPECT(cmdline_get(cmdline, "loglevel", value, sizeof(value)));
	KEXPECT_STREQ(value, "debug");
	KEXPECT(!cmdline_get(cmdline, "quiet", value, sizeof(value)));
	KEXPECT_STREQ(value, "");
}
