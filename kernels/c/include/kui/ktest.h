/*
 * カーネル内単体テストの仕組み。
 *
 * テスト用カーネル（KUI_KTEST を定義してビルドしたもの）は、起動後に
 * KTEST() で定義されたテストをすべて実行し、結果をシリアルへ出力してから
 * QEMU を成功または失敗で終了させる。
 *
 * 使い方:
 *   KTEST(string_memcpy_copies_bytes)
 *   {
 *       char buf[4];
 *       memcpy(buf, "abc", 4);
 *       KEXPECT_STREQ(buf, "abc");
 *   }
 *
 * テストの記述子は専用セクション .ktests に置かれ、リンカスクリプトが
 * その先頭と末尾に __ktests_start / __ktests_end の印を付ける。
 * 実行時はその間を走査するだけなので、テストを登録し忘れることがない。
 */
#ifndef KUI_KTEST_H
#define KUI_KTEST_H

#include <stdbool.h>
#include <stdint.h>

struct ktest_ctx {
	const char *name;
	int failures; /* このテスト内で失敗した確認の数 */
};

struct ktest {
	const char *name;
	void (*fn)(struct ktest_ctx *ctx);
};

#define KTEST(test_name)                                                         \
	static void ktest_fn_##test_name(struct ktest_ctx *ctx);                 \
	__attribute__((used, section(".ktests"), aligned(8))) static const struct ktest \
		ktest_desc_##test_name = { #test_name, ktest_fn_##test_name };   \
	static void ktest_fn_##test_name(struct ktest_ctx *ctx)

/* 確認に失敗したら記録してメッセージを出す（テストは続行する） */
void ktest_fail(struct ktest_ctx *ctx, const char *file, int line, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));

#define KEXPECT(cond)                                                            \
	do {                                                                     \
		if (!(cond))                                                     \
			ktest_fail(ctx, __FILE__, __LINE__, "%s", #cond);        \
	} while (0)

#define KEXPECT_EQ(a, b)                                                         \
	do {                                                                     \
		uint64_t ktest_a_ = (uint64_t)(a), ktest_b_ = (uint64_t)(b);     \
		if (ktest_a_ != ktest_b_)                                        \
			ktest_fail(ctx, __FILE__, __LINE__,                      \
				   "%s == %s (0x%llx != 0x%llx)", #a, #b,        \
				   (unsigned long long)ktest_a_,                 \
				   (unsigned long long)ktest_b_);                \
	} while (0)

#define KEXPECT_STREQ(a, b)                                                      \
	do {                                                                     \
		const char *ktest_a_ = (a), *ktest_b_ = (b);                     \
		if (strcmp(ktest_a_, ktest_b_) != 0)                             \
			ktest_fail(ctx, __FILE__, __LINE__,                      \
				   "%s == %s (\"%s\" != \"%s\")", #a, #b,        \
				   ktest_a_, ktest_b_);                          \
	} while (0)

/* 登録されたテストをすべて実行する。全て成功なら true */
bool ktest_run_all(void);

#endif
