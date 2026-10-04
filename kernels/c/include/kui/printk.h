/* カーネルのログ出力 */
#ifndef KUI_PRINTK_H
#define KUI_PRINTK_H

#include <stdarg.h>
#include <stdbool.h>

/*
 * ログをシリアルへ出力する。書式は kui/format.h を参照。
 * 1 回の呼び出しの出力は、割り込み処理の出力と混ざらないようロックで守られる。
 * printk の途中で起きた例外・NMI の処理から呼ばれた（再入）場合は、デッドロックを避けて
 * ロックなしで出す。
 */
int printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int vprintk(const char *fmt, va_list ap);

/*
 * ロックを取らずに出力する（緊急用）。他の出力と混ざることがある。
 * スピンロックの警告など、printk のロックを待っている最中にも出したいときに使う。
 */
int printk_emergency(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/*
 * panic 用のモードに切り替える。以後の printk はロックを取らない。
 * printk のロックを持ったまま panic しても（あるいはロックが壊れていても）表示できるように、
 * panic() が最初に呼ぶ。元には戻せない。
 */
void printk_set_panic_mode(void);
bool printk_in_panic_mode(void);

#ifdef KUI_KTEST
#include <stdint.h>

/* テスト専用: printk のロックを取る／放す（再入の検証用。printk.c を参照） */
uint64_t printk_ktest_hold_lock(void);
void printk_ktest_release_lock(uint64_t flags);
#endif

#endif
