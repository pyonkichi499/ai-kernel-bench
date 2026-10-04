/*
 * カーネルのログ出力。
 *
 * 書式化は format_v() に任せ、ここでは出力先（今はシリアルのみ）へ渡すだけ。
 * M6 で画面コンソールができたら、ここで画面にも出す。
 *
 * 排他制御: M1 から割り込みが入るので、通常の処理が printk の途中で割り込まれ、
 * 割り込み処理も printk すると、2 つの行が文字単位で混ざってしまう。
 * そこで 1 回の printk 全体を spin_lock_irqsave() で囲む。割り込みを禁止してから
 * ロックを取るので、割り込み処理が同じロックを待ってデッドロックすることもない。
 *
 * ロックを使わない経路が 3 つある:
 *   - panic モード: panic() が最初に切り替える。printk のロックを持ったまま panic
 *     しても、表示のためにロックを待って固まらないようにするため。
 *   - printk_emergency(): スピンロックの警告など、ロック待ちの最中に出したい表示用。
 *   - 再入: printk の途中で例外や NMI が起き（例: 書式化中の #PF、int3 の #BP）、その
 *     処理の中でまた printk した場合。割り込み禁止中に使用中のロックを待つと、単一 CPU では
 *     永久に待つ（spinlock.c はこれをデッドロックとして panic する）。すると本当の例外の
 *     報告が失われるので、ロックが取れなければ待たずにロックなしで出す。行が混ざることは
 *     あるが、報告が消えるよりよい。
 *     マルチ CPU に対応するとき（M6 以降）は、ロックの持ち主が自分の CPU のときだけ
 *     ロックなしで出し、他の CPU が持っているなら待つように変える必要がある。
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/format.h>
#include <kui/printk.h>
#include <kui/serial.h>
#include <kui/spinlock.h>

static struct spinlock printk_lock = SPINLOCK_INIT("printk");

/*
 * panic モードかどうか。一度 true になったら戻らない。
 * 割り込み処理からも読まれるので volatile にして、毎回メモリから読ませる。
 */
static volatile bool panic_mode;

static void printk_sink(char c, void *ctx)
{
	(void)ctx;
	serial_putc(c);
}

static int vprintk_unlocked(const char *fmt, va_list ap)
{
	return format_v(printk_sink, 0, fmt, ap);
}

int vprintk(const char *fmt, va_list ap)
{
	uint64_t flags;
	int n;

	if (panic_mode)
		return vprintk_unlocked(fmt, ap);

	/*
	 * 割り込みを禁止してから、待たずに取る。禁止中に取れないのは、この CPU の
	 * 割り込まれた側（printk の途中）が持っている場合だけ = 再入なので、ロックなしで出す。
	 */
	flags = cpu_irq_save();
	if (!spin_trylock(&printk_lock)) {
		n = vprintk_unlocked(fmt, ap);
		cpu_irq_restore(flags);
		return n;
	}
	n = vprintk_unlocked(fmt, ap);
	spin_unlock(&printk_lock);
	cpu_irq_restore(flags);
	return n;
}

int printk(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vprintk(fmt, ap);
	va_end(ap);
	return n;
}

int printk_emergency(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vprintk_unlocked(fmt, ap);
	va_end(ap);
	return n;
}

void printk_set_panic_mode(void)
{
	panic_mode = true;
}

bool printk_in_panic_mode(void)
{
	return panic_mode;
}

#ifdef KUI_KTEST
/*
 * テスト専用: printk のロックを取る／放す。
 * 「printk の途中で例外が起きた」状況を、カーネル内テストで再現するために使う。
 */
uint64_t printk_ktest_hold_lock(void)
{
	return spin_lock_irqsave(&printk_lock);
}

void printk_ktest_release_lock(uint64_t flags)
{
	spin_unlock_irqrestore(&printk_lock, flags);
}
#endif
