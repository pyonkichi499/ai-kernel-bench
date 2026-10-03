/* src/lib/format.c のテスト */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/format.h>

#include "test.h"

/* ksnprintf の結果と戻り値を確認する */
#define EXPECT_FMT(expected, ...)                                                \
	do {                                                                     \
		char buf_[256];                                                  \
		int n_ = ksnprintf(buf_, sizeof(buf_), __VA_ARGS__);             \
		EXPECT_STREQ(buf_, expected);                                    \
		EXPECT_EQ(n_, (int)strlen(expected));                            \
	} while (0)

/*
 * ホストの snprintf と同じ結果になることを確認する。
 * 対応している書式の範囲では、C 標準と同じ動作をするのが仕様。
 */
#define EXPECT_SAME_AS_LIBC(...)                                                 \
	do {                                                                     \
		char ours_[256], libc_[256];                                     \
		int n1_ = ksnprintf(ours_, sizeof(ours_), __VA_ARGS__);          \
		int n2_ = snprintf(libc_, sizeof(libc_), __VA_ARGS__);           \
		EXPECT_STREQ(ours_, libc_);                                      \
		EXPECT_EQ(n1_, n2_);                                             \
	} while (0)

TEST(format_plain_text)
{
	EXPECT_FMT("", "");
	EXPECT_FMT("hello", "hello");
	EXPECT_FMT("100%", "100%%");
}

TEST(format_char_and_string)
{
	EXPECT_FMT("x", "%c", 'x');
	EXPECT_FMT("[  x]", "[%3c]", 'x');
	EXPECT_FMT("[x  ]", "[%-3c]", 'x');
	EXPECT_FMT("abc", "%s", "abc");
	EXPECT_FMT("[   abc]", "[%6s]", "abc");
	EXPECT_FMT("[abc   ]", "[%-6s]", "abc");
	EXPECT_FMT("[abcdef]", "[%3s]", "abcdef"); /* 幅より長ければそのまま */
	EXPECT_FMT("(null)", "%s", (char *)NULL);
}

TEST(format_string_precision)
{
	EXPECT_FMT("ab", "%.2s", "abcdef");
	EXPECT_FMT("", "%.0s", "abcdef");
	EXPECT_FMT("abc", "%.10s", "abc");
	EXPECT_FMT("[   ab]", "[%5.2s]", "abcdef");
	EXPECT_FMT("ab", "%.*s", 2, "abcdef");
	EXPECT_FMT("abcdef", "%.*s", -1, "abcdef"); /* 負の精度は指定なし */

	/* 精度があれば終端のない配列も読める（範囲外アクセスは ASan が検出する） */
	char no_nul[3] = {'x', 'y', 'z'};
	EXPECT_FMT("xyz", "%.3s", no_nul);
}

TEST(format_signed_decimal)
{
	EXPECT_FMT("0", "%d", 0);
	EXPECT_FMT("42", "%d", 42);
	EXPECT_FMT("-42", "%i", -42);
	EXPECT_FMT("2147483647", "%d", INT_MAX);
	EXPECT_FMT("-2147483648", "%d", INT_MIN);
	EXPECT_FMT("-9223372036854775808", "%lld", LLONG_MIN);
	EXPECT_FMT("9223372036854775807", "%lld", LLONG_MAX);
	EXPECT_FMT("-9223372036854775808", "%ld", LONG_MIN);
	EXPECT_FMT("-1", "%zd", (ptrdiff_t)-1);
}

TEST(format_length_modifiers_truncate)
{
	/* hh / h は int で渡された値を char / short に切り詰めて表示する */
	EXPECT_FMT("-128", "%hhd", 128);
	EXPECT_FMT("255", "%hhu", -1);
	EXPECT_FMT("-1", "%hd", 65535);
	EXPECT_FMT("65535", "%hu", -1);
	EXPECT_FMT("ff", "%hhx", 0x1ff);
}

TEST(format_unsigned_and_hex)
{
	EXPECT_FMT("4294967295", "%u", UINT_MAX);
	EXPECT_FMT("18446744073709551615", "%llu", ULLONG_MAX);
	EXPECT_FMT("18446744073709551615", "%zu", SIZE_MAX);
	EXPECT_FMT("deadbeef", "%x", 0xdeadbeefu);
	EXPECT_FMT("DEADBEEF", "%X", 0xdeadbeefu);
	EXPECT_FMT("0", "%x", 0);
	EXPECT_FMT("ffffffffffffffff", "%llx", ULLONG_MAX);
	EXPECT_FMT("ffffffffffffffff", "%lx", ULONG_MAX);
}

TEST(format_width_and_flags)
{
	EXPECT_FMT("[   42]", "[%5d]", 42);
	EXPECT_FMT("[42   ]", "[%-5d]", 42);
	EXPECT_FMT("[00042]", "[%05d]", 42);
	EXPECT_FMT("[-0042]", "[%05d]", -42); /* 符号はゼロ埋めの前 */
	EXPECT_FMT("[  -42]", "[%5d]", -42);
	/* '-' があればゼロ埋めは無視（コンパイラはこの組み合わせを警告するので止める） */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
	EXPECT_FMT("[-42  ]", "[%-05d]", -42);
#pragma clang diagnostic pop
	EXPECT_FMT("[000000ff]", "[%08x]", 0xff);
	EXPECT_FMT("[   ff]", "[%*x]", 5, 0xff);
	EXPECT_FMT("[ff   ]", "[%*x]", -5, 0xff); /* 負の幅は左寄せ */
	EXPECT_FMT("[12345]", "[%3d]", 12345);
}

TEST(format_pointer)
{
	EXPECT_FMT("0x0000000000000000", "%p", (void *)0);
	EXPECT_FMT("0xffffffff80001000", "%p", (void *)(uintptr_t)0xffffffff80001000ull);
	EXPECT_FMT("[  0x00000000000000ab]", "[%20p]", (void *)(uintptr_t)0xab);
	EXPECT_FMT("[0x00000000000000ab  ]", "[%-20p]", (void *)(uintptr_t)0xab);
}

TEST(format_matches_libc)
{
	EXPECT_SAME_AS_LIBC("%d %i %u %x %X", -7, 123, 456u, 0xabcu, 0xabcu);
	EXPECT_SAME_AS_LIBC("[%10d][%-10d][%010d]", -12345, -12345, -12345);
	EXPECT_SAME_AS_LIBC("[%5s][%-5s][%.1s]", "ab", "ab", "ab");
	EXPECT_SAME_AS_LIBC("%lld %llu %llx", LLONG_MIN, ULLONG_MAX, ULLONG_MAX);
	EXPECT_SAME_AS_LIBC("%hhd %hhu %hd %hu", 200, 300, 70000, 70000);
	EXPECT_SAME_AS_LIBC("%zu %zx", SIZE_MAX, (size_t)0x1234);
	EXPECT_SAME_AS_LIBC("[%*d][%-*d]", 6, 42, 6, 42);
	EXPECT_SAME_AS_LIBC("[%c][%3c][%-3c]", 'a', 'b', 'c');
	EXPECT_SAME_AS_LIBC("%%%d%%", 5);
}

TEST(format_truncation)
{
	char buf[6];
	int n = ksnprintf(buf, sizeof(buf), "hello world");

	/* 常に終端され、戻り値は切り詰めなしの長さ */
	EXPECT_STREQ(buf, "hello");
	EXPECT_EQ(n, 11);

	n = ksnprintf(buf, sizeof(buf), "%d", 1234567);
	EXPECT_STREQ(buf, "12345");
	EXPECT_EQ(n, 7);
}

TEST(format_size_one_and_zero)
{
	char buf[4] = "zzz";

	EXPECT_EQ(ksnprintf(buf, 1, "abc"), 3);
	EXPECT_STREQ(buf, "");

	char untouched[4] = "zzz";
	EXPECT_EQ(ksnprintf(untouched, 0, "abc%d", 12), 5);
	EXPECT_STREQ(untouched, "zzz");

	/* size が 0 なら buf は NULL でもよい（長さの見積もりに使える） */
	EXPECT_EQ(ksnprintf(NULL, 0, "%s", "hello"), 5);
}

/* 未対応の指定は、そのまま出力するのが仕様。コンパイラの書式チェックはここだけ止める */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
#pragma clang diagnostic ignored "-Wformat-invalid-specifier"
#pragma clang diagnostic ignored "-Wformat-extra-args"
TEST(format_unknown_specifier_is_printed_verbatim)
{
	EXPECT_FMT("%q", "%q");
	EXPECT_FMT("a%5qb", "a%5qb");
	EXPECT_FMT("abc%", "abc%"); /* 末尾の '%' 単独 */
	EXPECT_FMT("x%-0", "x%-0"); /* 変換文字なしで終端 */
}
#pragma clang diagnostic pop

/* format_v を直接使い、任意の出力先へ書けることを確認する */
struct counter {
	int calls;
	char last;
};

static void count_sink(char c, void *ctx)
{
	struct counter *ct = ctx;

	ct->calls++;
	ct->last = c;
}

static int call_format_v(struct counter *ct, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static int call_format_v(struct counter *ct, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	int n = format_v(count_sink, ct, fmt, ap);
	va_end(ap);
	return n;
}

TEST(format_v_uses_sink)
{
	struct counter ct = {0, 0};
	int n = call_format_v(&ct, "id=%d!", 1234);

	EXPECT_EQ(n, 8);
	EXPECT_EQ(ct.calls, 8);
	EXPECT_EQ(ct.last, '!');
}

/* 異常に大きな幅は上限（100000）で丸められ、出力文字数の int があふれない */
TEST(format_huge_width_is_clamped)
{
	char buf[8];

	EXPECT_EQ(ksnprintf(buf, sizeof(buf), "%999999999d", 1), 100000);
	EXPECT_STREQ(buf, "       ");
	EXPECT_EQ(ksnprintf(buf, sizeof(buf), "%*d", INT_MAX, 1), 100000);
	/* INT_MIN の幅は「左寄せ・上限の幅」として扱う（-INT_MIN はあふれるので計算しない） */
	EXPECT_EQ(ksnprintf(buf, sizeof(buf), "%*d", INT_MIN, 7), 100000);
	EXPECT_STREQ(buf, "7      ");
}

/* 負の精度（'*' で渡したもの）は「指定なし」と同じ */
TEST(format_negative_star_precision_means_none)
{
	char buf[16];

	EXPECT_EQ(ksnprintf(buf, sizeof(buf), "%.*s", -1, "abc"), 3);
	EXPECT_STREQ(buf, "abc");
}

/* %c で '\0' を出力しても、戻り値は 1 文字分数える */
TEST(format_char_nul_is_counted)
{
	char buf[4] = {'x', 'x', 'x', 'x'};

	EXPECT_EQ(ksnprintf(buf, sizeof(buf), "%c", '\0'), 1);
	EXPECT_EQ(buf[0], '\0');
	EXPECT_EQ(buf[1], '\0');
}

/* buf が NULL でも size が 0 なら何も書かず、必要な文字数を返す */
TEST(format_null_buffer_with_size_zero)
{
	EXPECT_EQ(ksnprintf(NULL, 0, "%d-%s", 42, "ab"), 5);
}
