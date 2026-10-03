/*
 * ホスト単体テスト用の最小フレームワーク。
 *
 * ハードウェアに依存しないカーネルのコード（src/lib/ など）を、普通の Linux
 * プログラムとしてビルドしてテストする。AddressSanitizer と UBSan を有効にして
 * ビルドするので、範囲外アクセスや未定義動作もここで見つかる。
 *
 * 使い方:
 *   TEST(memcpy_copies_bytes)
 *   {
 *       EXPECT_EQ(1 + 1, 2);
 *   }
 *
 * TEST() で定義した関数は、プログラム開始前（constructor 属性）に自動で登録され、
 * test_main.c の main() が順に実行する。確認に失敗してもテストは続行し、
 * 最後に失敗があれば終了コード 1 で終わる。
 */
#ifndef KUI_HOST_TEST_H
#define KUI_HOST_TEST_H

#include <stdio.h>
#include <string.h>

void test_register(const char *name, void (*fn)(void));
void test_fail(const char *file, int line, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

#define TEST(name)                                                               \
	static void test_fn_##name(void);                                        \
	__attribute__((constructor)) static void test_reg_##name(void)           \
	{                                                                        \
		test_register(#name, test_fn_##name);                           \
	}                                                                        \
	static void test_fn_##name(void)

#define EXPECT(cond)                                                             \
	do {                                                                     \
		if (!(cond))                                                     \
			test_fail(__FILE__, __LINE__, "%s", #cond);              \
	} while (0)

/* 整数として比較する（符号付き） */
#define EXPECT_EQ(a, b)                                                          \
	do {                                                                     \
		long long test_a_ = (long long)(a), test_b_ = (long long)(b);    \
		if (test_a_ != test_b_)                                          \
			test_fail(__FILE__, __LINE__, "%s == %s (%lld != %lld)", #a, \
				  #b, test_a_, test_b_);                         \
	} while (0)

/* 整数として比較する（符号なし。ポインタやサイズ向け） */
#define EXPECT_EQ_U(a, b)                                                        \
	do {                                                                     \
		unsigned long long test_a_ = (unsigned long long)(a);            \
		unsigned long long test_b_ = (unsigned long long)(b);            \
		if (test_a_ != test_b_)                                          \
			test_fail(__FILE__, __LINE__, "%s == %s (0x%llx != 0x%llx)", \
				  #a, #b, test_a_, test_b_);                     \
	} while (0)

#define EXPECT_STREQ(a, b)                                                       \
	do {                                                                     \
		const char *test_a_ = (a), *test_b_ = (b);                       \
		if (strcmp(test_a_, test_b_) != 0)                               \
			test_fail(__FILE__, __LINE__, "%s == %s (\"%s\" != \"%s\")", \
				  #a, #b, test_a_, test_b_);                     \
	} while (0)

#define EXPECT_MEMEQ(a, b, n)                                                    \
	do {                                                                     \
		if (memcmp((a), (b), (n)) != 0)                                  \
			test_fail(__FILE__, __LINE__, "%s と %s の先頭 %s バイトが一致しない", \
				  #a, #b, #n);                                   \
	} while (0)

#endif
