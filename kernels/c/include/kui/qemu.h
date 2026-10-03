/*
 * QEMU の isa-debug-exit デバイスを使って QEMU を終了させる。
 *
 * QEMU を "-device isa-debug-exit,iobase=0xf4,iosize=0x04" 付きで起動すると、
 * I/O ポート 0xf4 に値 v を書いたとき QEMU が終了コード (v << 1) | 1 で終了する。
 * テストの合否を QEMU の終了コードで外部へ伝えるのに使う。
 * 実機や、このデバイスがない QEMU では何も起きない。
 */
#ifndef KUI_QEMU_H
#define KUI_QEMU_H

enum qemu_exit_code {
	QEMU_EXIT_SUCCESS = 0x10, /* QEMU の終了コードは 33 */
	QEMU_EXIT_FAILURE = 0x11, /* QEMU の終了コードは 35 */
};

/* QEMU を終了させる。終了できなかった場合（実機など）は CPU を停止する */
_Noreturn void qemu_exit(enum qemu_exit_code code);

#endif
