/*
 * 8259 PIC（Programmable Interrupt Controller）の無効化。
 *
 * PC は 2 個の 8259 を親子につないで IRQ 0〜15 を扱っていた。Kui は I/O APIC を使うので
 * 8259 は使わないが、電源投入時の設定ではベクタ 0x08〜0x0f に割り込みを出す
 * （CPU の例外 #DF などと重なる）。万一割り込みが漏れても例外と区別できるよう、
 * まず 0x20〜0x2f へずらし（初期化コマンド ICW1〜ICW4）、そのうえで全 IRQ をマスクする。
 */
#include <kui/arch/x86_64/apic.h>
#include <kui/arch/x86_64/interrupt.h>
#include <kui/arch/x86_64/io.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xa0
#define PIC2_DATA 0xa1

#define ICW1_INIT 0x10 /* 初期化を始める */
#define ICW1_ICW4 0x01 /* ICW4 を送る */
#define ICW4_8086 0x01 /* 8086 モード */

/* 古いハードウェアは I/O が遅いので、使われていないポート 0x80 に書いて少し待つ */
static void io_wait(void)
{
	outb(0x80, 0);
}

void pic_disable(void)
{
	outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
	io_wait();
	outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
	io_wait();
	outb(PIC1_DATA, VEC_IRQ_BASE); /* ICW2: 親の IRQ 0〜7 → 0x20〜0x27 */
	io_wait();
	outb(PIC2_DATA, VEC_IRQ_BASE + 8); /* ICW2: 子の IRQ 8〜15 → 0x28〜0x2f */
	io_wait();
	outb(PIC1_DATA, 1 << 2); /* ICW3: 親の IRQ 2 に子がつながっている */
	io_wait();
	outb(PIC2_DATA, 2); /* ICW3: 子は親の IRQ 2 につながっている */
	io_wait();
	outb(PIC1_DATA, ICW4_8086);
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();

	/* 全 IRQ をマスクする */
	outb(PIC1_DATA, 0xff);
	outb(PIC2_DATA, 0xff);
}
