/*
 * GDT のセレクタと TSS の設定（x86_64）。並びの理由は src/arch/x86_64/gdt.c を参照。
 * gdt_init() の宣言は kui/arch/x86_64/interrupt.h にある。
 */
#ifndef KUI_ARCH_X86_64_GDT_H
#define KUI_ARCH_X86_64_GDT_H

#include <stdint.h>

#define GDT_SEL_KERNEL_CODE 0x08
#define GDT_SEL_KERNEL_DATA 0x10
#define GDT_SEL_USER_DATA   0x1b /* 0x18 | ring 3 */
#define GDT_SEL_USER_CODE   0x23 /* 0x20 | ring 3 */
#define GDT_SEL_TSS         0x28

/* IST の番号（IDT のエントリに書く値。1〜7） */
#define IST_DOUBLE_FAULT  1
#define IST_NMI           2
#define IST_MACHINE_CHECK 3

/* ユーザーモードから割り込まれたときに使うカーネルスタックの末尾（TSS の rsp0）を設定する */
void gdt_set_kernel_stack(uint64_t rsp0);

#endif
