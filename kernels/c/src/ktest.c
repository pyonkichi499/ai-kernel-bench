/*
 * カーネル内単体テストの実行部（使い方は kui/ktest.h を参照）。
 *
 * テスト用カーネルにだけリンクされる。
 */
#include <stdarg.h>
#include <stdbool.h>

#include <kui/ktest.h>
#include <kui/printk.h>

/* リンカスクリプトが .ktests セクションの先頭と末尾に置く印 */
extern const struct ktest __ktests_start[];
extern const struct ktest __ktests_end[];

void ktest_fail(struct ktest_ctx *ctx, const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	ctx->failures++;
	printk("ktest:   %s:%d: ", file, line);
	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
	printk("\n");
}

bool ktest_run_all(void)
{
	const struct ktest *t;
	int count = (int)(__ktests_end - __ktests_start);
	int passed = 0, failed = 0;

	printk("ktest: running %d tests\n", count);
	for (t = __ktests_start; t < __ktests_end; t++) {
		struct ktest_ctx ctx = {.name = t->name, .failures = 0};

		t->fn(&ctx);
		if (ctx.failures == 0) {
			passed++;
			printk("ktest: PASS %s\n", t->name);
		} else {
			failed++;
			printk("ktest: FAIL %s\n", t->name);
		}
	}
	printk("ktest: %d passed, %d failed\n", passed, failed);

	/* テストが 1 件もないのは、登録の仕組みが壊れている可能性が高いので失敗扱い */
	return failed == 0 && count > 0;
}
