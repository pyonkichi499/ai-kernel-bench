/*
 * panic: 回復できないエラーで止まる。
 *
 * 通常のカーネルでは CPU を停止させる。テスト用カーネル（KUI_KTEST）では、
 * 外から失敗が分かるように QEMU を「失敗」の終了コードで終わらせる。
 */
#include <stdarg.h>
#include <stdint.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/panic.h>
#include <kui/printk.h>
#include <kui/qemu.h>
#include <kui/serial.h>

/* panic 中にさらに panic した（例: 表示処理の中で UBSan が反応した）かどうか */
static int panic_depth;

/*
 * 書式化を通さずに文字列をそのままシリアルへ出す。
 * 二重 panic は書式化（printk）自体の不具合で起きている可能性があるので、使わない。
 */
static void panic_puts_raw(const char *s)
{
	while (*s)
		serial_putc(*s++);
}

_Noreturn void panic(const char *fmt, ...)
{
	va_list ap;

	cpu_disable_interrupts();
	if (++panic_depth > 1) {
		/* 二重 panic。これ以上の表示は危ないので、最小限で止まる */
		if (panic_depth == 2)
			panic_puts_raw("PANIC: nested panic\n");
		goto stop;
	}

	printk("PANIC: ");
	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
	printk("\n");
	/* どこから panic が呼ばれたか。llvm-addr2line でソースの行に変換できる */
	printk("  called from 0x%llx\n",
	       (unsigned long long)(uintptr_t)__builtin_return_address(0));

stop:
#ifdef KUI_KTEST
	qemu_exit(QEMU_EXIT_FAILURE);
#else
	cpu_halt_forever();
#endif
}
