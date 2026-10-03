/*
 * カーネルのログ出力。
 *
 * 書式化は format_v() に任せ、ここでは出力先（今はシリアルのみ）へ渡すだけ。
 * M6 で画面コンソールができたら、ここで画面にも出す。
 *
 * まだ割り込みもマルチタスクもないので排他制御は不要。M1 で割り込みを
 * 有効にするときに、出力が混ざらないようロックを入れる。
 */
#include <stdarg.h>

#include <kui/format.h>
#include <kui/printk.h>
#include <kui/serial.h>

static void printk_sink(char c, void *ctx)
{
	(void)ctx;
	serial_putc(c);
}

int vprintk(const char *fmt, va_list ap)
{
	return format_v(printk_sink, 0, fmt, ap);
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
