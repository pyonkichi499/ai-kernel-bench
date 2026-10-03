/* 回復できないエラーでカーネルを停止させる */
#ifndef KUI_PANIC_H
#define KUI_PANIC_H

/*
 * "PANIC: <メッセージ>" をシリアルへ出力して停止する。
 * テスト用カーネル（KUI_KTEST）では、停止の代わりに QEMU を「失敗」で終了させる。
 */
_Noreturn void panic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* 条件が偽なら panic する。リリースビルドでも無効化しない */
#define KASSERT(cond)                                                              \
	do {                                                                       \
		if (!(cond))                                                       \
			panic("assertion failed: %s (%s:%d)", #cond, __FILE__, __LINE__); \
	} while (0)

#endif
