/*
 * テストの仕組みそのものの確認。
 *
 * 「失敗するはずの確認が、ちゃんと失敗として数えられる」ことを確かめないと、
 * どんなバグがあっても全部 PASS と表示される壊れたテスト基盤に気づけない。
 */
#include <kui/ktest.h>
#include <kui/printk.h>
#include <kui/string.h>

/* 失敗を数えるだけの内側の「テスト」。KEXPECT 系マクロは変数名 ctx を使う */
static int count_failures(void (*body)(struct ktest_ctx *ctx))
{
	struct ktest_ctx inner = {.name = "inner", .failures = 0};

	body(&inner);
	return inner.failures;
}

static void passing_body(struct ktest_ctx *ctx)
{
	KEXPECT(1 + 1 == 2);
	KEXPECT_EQ(42, 42);
	KEXPECT_STREQ("kui", "kui");
}

static void failing_body(struct ktest_ctx *ctx)
{
	/* 以下の 3 つはすべて失敗する。失敗メッセージが出力されるのは想定どおり */
	KEXPECT(1 + 1 == 3);
	KEXPECT_EQ(1, 2);
	KEXPECT_STREQ("kui", "yuzhu");
}

KTEST(ktest_counts_failures)
{
	KEXPECT_EQ(count_failures(passing_body), 0);
	printk("ktest:   (次の 3 行は、失敗の検出を確かめるための意図的な失敗)\n");
	KEXPECT_EQ(count_failures(failing_body), 3);
}
