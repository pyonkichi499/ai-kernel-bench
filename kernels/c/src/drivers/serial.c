/*
 * シリアルポート（UART 16550 互換、COM1）のドライバ。
 *
 * UART は I/O ポート 0x3F8 から始まる 8 個のレジスタで操作する。
 * QEMU では "-serial stdio" などでホストの端末やファイルにつながる。
 */
#include <stdbool.h>
#include <stdint.h>

#include <kui/arch/x86_64/io.h>
#include <kui/serial.h>

#define COM1 0x3F8

/* レジスタのオフセット（DLAB はライン制御レジスタのビット 7） */
#define REG_DATA        0 /* 送受信データ（DLAB=1 のときは分周比の下位） */
#define REG_INT_ENABLE  1 /* 割り込み許可（DLAB=1 のときは分周比の上位） */
#define REG_FIFO_CTRL   2 /* FIFO 制御 */
#define REG_LINE_CTRL   3 /* ライン制御（データ長など） */
#define REG_MODEM_CTRL  4 /* モデム制御 */
#define REG_LINE_STATUS 5 /* ライン状態 */

#define LINE_STATUS_THR_EMPTY 0x20 /* 送信バッファが空いた */

enum serial_state {
	SERIAL_UNINITIALIZED,
	SERIAL_PRESENT,
	SERIAL_ABSENT,
};

static enum serial_state state = SERIAL_UNINITIALIZED;

bool serial_init(void)
{
	if (state != SERIAL_UNINITIALIZED)
		return state == SERIAL_PRESENT;

	outb(COM1 + REG_INT_ENABLE, 0x00); /* 割り込みは使わない */
	outb(COM1 + REG_LINE_CTRL, 0x80);  /* DLAB=1: 分周比を設定するモードへ */
	outb(COM1 + REG_DATA, 0x01);       /* 分周比 1 = 115200bps（下位） */
	outb(COM1 + REG_INT_ENABLE, 0x00); /*                        （上位） */
	outb(COM1 + REG_LINE_CTRL, 0x03);  /* DLAB=0、8 ビット、パリティなし、ストップ 1 */
	outb(COM1 + REG_FIFO_CTRL, 0xC7);  /* FIFO 有効、送受信 FIFO をクリア */

	/*
	 * ループバックモードで 1 バイト送り、同じ値が返ってくるかで存在を確かめる。
	 * シリアルポートのない実機で、存在しないポートを待ち続けて固まるのを防ぐ。
	 */
	outb(COM1 + REG_MODEM_CTRL, 0x1E);
	outb(COM1 + REG_DATA, 0xAE);
	if (inb(COM1 + REG_DATA) != 0xAE) {
		state = SERIAL_ABSENT;
		return false;
	}

	/* 通常モードへ戻す（DTR、RTS、OUT1、OUT2 を有効） */
	outb(COM1 + REG_MODEM_CTRL, 0x0F);
	state = SERIAL_PRESENT;
	return true;
}

/*
 * 送信バッファが空くのを待つ回数の上限。115200bps なら 1 文字は約 87 マイクロ秒で
 * 送り終わるので、十分に大きな値にしておく。壊れた UART で永久に固まらないための保険。
 */
#define THR_WAIT_LIMIT 1000000

static void serial_write_raw(char c)
{
	for (int i = 0; i < THR_WAIT_LIMIT; i++) {
		if (inb(COM1 + REG_LINE_STATUS) & LINE_STATUS_THR_EMPTY)
			break;
	}
	outb(COM1 + REG_DATA, (uint8_t)c);
}

void serial_putc(char c)
{
	if (state == SERIAL_UNINITIALIZED)
		serial_init();
	if (state != SERIAL_PRESENT)
		return;

	if (c == '\n')
		serial_write_raw('\r');
	serial_write_raw(c);
}
