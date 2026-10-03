/*
 * 書式化エンジンをホストの snprintf と突き合わせる、ランダム入力のテスト。
 *
 * 書式指定（フラグ、幅、精度、長さ修飾子、変換）と引数の値、出力先の大きさを
 * 疑似乱数で組み合わせ、ksnprintf と libc の snprintf の結果（戻り値と出力内容）が
 * 一致することを多数回確かめる。乱数の種は固定なので、失敗は毎回再現する。
 *
 * C 標準で未定義の組み合わせ（%s に '0' フラグなど）や、意図的に libc と
 * 違う仕様にしているもの（%p、整数の精度）は生成しない。
 */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kui/format.h>

#include "test.h"

#define FUZZ_ITERATIONS 200000
#define FUZZ_BUF 96

/* xorshift64: 小さくて再現性のある疑似乱数 */
static uint64_t rng_state;

static uint64_t rng(void)
{
	uint64_t x = rng_state;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rng_state = x;
	return x;
}

static unsigned rng_below(unsigned n)
{
	return (unsigned)(rng() % n);
}

/* 境界値を多めに混ぜた 64 ビット値 */
static uint64_t rng_value(void)
{
	static const uint64_t edges[] = {
		0,
		1,
		9,
		10,
		0x7f,
		0x80,
		0xff,
		0x7fff,
		0x8000,
		0xffff,
		0x7fffffff,
		0x80000000,
		0xffffffff,
		0x7fffffffffffffffULL,
		0x8000000000000000ULL,
		0xffffffffffffffffULL,
	};

	if (rng_below(3) == 0)
		return edges[rng_below(sizeof(edges) / sizeof(edges[0]))];
	/* 桁数がばらけるよう、ランダムな幅でマスクする */
	return rng() >> rng_below(64);
}

enum conv_kind {
	CONV_SIGNED,
	CONV_UNSIGNED,
	CONV_CHAR,
	CONV_STRING,
	CONV_PERCENT
};

static const char *const strings[] = {"", "a", "hello", "日本語", "with space", "0123456789abcdef"};

TEST(format_fuzz_matches_libc_snprintf)
{
	int mismatches = 0;

	rng_state = 0x9e3779b97f4a7c15ULL;
	for (int iter = 0; iter < FUZZ_ITERATIONS && mismatches < 10; iter++) {
		char fmt[64];
		int fl = 0;
		enum conv_kind kind;
		const char *conv;
		const char *lenmod = "";

		switch (rng_below(5)) {
		case 0:
			kind = CONV_SIGNED;
			conv = rng_below(2) ? "d" : "i";
			break;
		case 1:
			kind = CONV_UNSIGNED;
			conv = (const char *[]){"u", "x", "X"}[rng_below(3)];
			break;
		case 2:
			kind = CONV_CHAR;
			conv = "c";
			break;
		case 3:
			kind = CONV_STRING;
			conv = "s";
			break;
		default:
			kind = CONV_PERCENT;
			conv = "%";
			break;
		}
		if (kind == CONV_SIGNED || kind == CONV_UNSIGNED)
			lenmod = (const char *[]){"", "hh", "h", "l", "ll", "z"}[rng_below(6)];

		/* 前置きの文字列 */
		if (rng_below(2))
			fl += snprintf(fmt + fl, sizeof(fmt) - fl, "<%c>",
				       'A' + (char)rng_below(26));
		fmt[fl++] = '%';

		bool numeric = kind == CONV_SIGNED || kind == CONV_UNSIGNED;
		bool use_star_width = false, use_star_prec = false;
		int star_width = 0, star_prec = 0;

		if (kind != CONV_PERCENT) {
			/* フラグ。'0' は数値の変換にだけ付ける（%s、%c では C 標準で未定義） */
			if (rng_below(3) == 0)
				fmt[fl++] = '-';
			if (numeric && rng_below(3) == 0)
				fmt[fl++] = '0';
			/* 幅 */
			switch (rng_below(3)) {
			case 0:
				break;
			case 1:
				fl += snprintf(fmt + fl, sizeof(fmt) - fl, "%u", rng_below(40));
				break;
			default:
				use_star_width = true;
				star_width = (int)rng_below(81) - 40;
				fmt[fl++] = '*';
				break;
			}
			/* 精度は %s だけ（整数の精度は意図的に未対応） */
			if (kind == CONV_STRING) {
				switch (rng_below(3)) {
				case 0:
					break;
				case 1:
					fl += snprintf(fmt + fl, sizeof(fmt) - fl, ".%u",
						       rng_below(12));
					break;
				default:
					use_star_prec = true;
					star_prec = (int)rng_below(25) - 8; /* 負なら「指定なし」 */
					fmt[fl++] = '.';
					fmt[fl++] = '*';
					break;
				}
			}
		}
		fl += snprintf(fmt + fl, sizeof(fmt) - fl, "%s%s", lenmod, conv);
		/* 後置きの文字列 */
		if (rng_below(2))
			fl += snprintf(fmt + fl, sizeof(fmt) - fl, "[end]");
		fmt[fl] = '\0';

		size_t size = rng_below(4) == 0 ? rng_below(8) : rng_below(FUZZ_BUF);
		char got[FUZZ_BUF], want[FUZZ_BUF];
		int got_n = 0, want_n = 0;
		uint64_t v = rng_value();
		const char *str = strings[rng_below(sizeof(strings) / sizeof(strings[0]))];
		char ch = (char)(' ' + rng_below(95));

		memset(got, 0x5a, sizeof(got));
		memset(want, 0x5a, sizeof(want));

		/*
		 * 実際の引数の型を書式と一致させて両方に渡す。
		 * 幅・精度の '*' の有無で引数の並びが変わるので、場合分けしてマクロで呼ぶ。
		 */
#define CALL_BOTH(...)                                                                             \
	do {                                                                                       \
		if (use_star_width && use_star_prec) {                                             \
			got_n = ksnprintf(got, size, fmt, star_width, star_prec, __VA_ARGS__);     \
			want_n = snprintf(want, size, fmt, star_width, star_prec, __VA_ARGS__);    \
		} else if (use_star_width) {                                                       \
			got_n = ksnprintf(got, size, fmt, star_width, __VA_ARGS__);                \
			want_n = snprintf(want, size, fmt, star_width, __VA_ARGS__);               \
		} else if (use_star_prec) {                                                        \
			got_n = ksnprintf(got, size, fmt, star_prec, __VA_ARGS__);                 \
			want_n = snprintf(want, size, fmt, star_prec, __VA_ARGS__);                \
		} else {                                                                           \
			got_n = ksnprintf(got, size, fmt, __VA_ARGS__);                            \
			want_n = snprintf(want, size, fmt, __VA_ARGS__);                           \
		}                                                                                  \
	} while (0)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
		switch (kind) {
		case CONV_SIGNED:
			if (lenmod[0] == 'l' && lenmod[1] == 'l')
				CALL_BOTH((long long)v);
			else if (lenmod[0] == 'l')
				CALL_BOTH((long)v);
			else if (lenmod[0] == 'z')
				CALL_BOTH((ptrdiff_t)v);
			else
				CALL_BOTH((int)v);
			break;
		case CONV_UNSIGNED:
			if (lenmod[0] == 'l' && lenmod[1] == 'l')
				CALL_BOTH((unsigned long long)v);
			else if (lenmod[0] == 'l')
				CALL_BOTH((unsigned long)v);
			else if (lenmod[0] == 'z')
				CALL_BOTH((size_t)v);
			else
				CALL_BOTH((unsigned int)v);
			break;
		case CONV_CHAR:
			CALL_BOTH(ch);
			break;
		case CONV_STRING:
			CALL_BOTH(str);
			break;
		case CONV_PERCENT:
			/* 引数なしの "%%"。CALL_BOTH は引数が必要なので、使われないダミーを渡す */
			got_n = ksnprintf(got, size, fmt, 0);
			want_n = snprintf(want, size, fmt, 0);
			break;
		}
#pragma clang diagnostic pop
#undef CALL_BOTH

		/* 出力先の大きさ分（size 以降は書かれていないこと）まで比べる */
		if (got_n != want_n || memcmp(got, want, sizeof(got)) != 0) {
			mismatches++;
			test_fail(
				__FILE__, __LINE__,
				"iter %d: fmt=\"%s\" size=%zu: got %d \"%.*s\" / want %d \"%.*s\"",
				iter, fmt, size, got_n, (int)(size ? size - 1 : 0), got, want_n,
				(int)(size ? size - 1 : 0), want);
		}
	}
}
