/*
 * x86 の I/O ポート命令（in / out）。
 *
 * x86 には、メモリとは別に「I/O ポート」という 16 ビットのアドレス空間があり、
 * シリアルポートなどの古いデバイスはここに置かれている。
 */
#ifndef KUI_ARCH_X86_64_IO_H
#define KUI_ARCH_X86_64_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
	return value;
}

#endif
