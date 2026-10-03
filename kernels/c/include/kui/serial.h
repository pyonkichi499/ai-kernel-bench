/* シリアルポート（UART 16550 互換、COM1）への出力 */
#ifndef KUI_SERIAL_H
#define KUI_SERIAL_H

#include <stdbool.h>

/*
 * COM1 を 115200bps、8 ビット、パリティなし、ストップビット 1 で初期化する。
 * 何度呼んでもよい。ポートが存在しなければ false を返し、以後の出力は捨てられる。
 */
bool serial_init(void);

/* 1 文字送る。'\n' は端末で正しく改行されるよう "\r\n" に変換する */
void serial_putc(char c);

#endif
