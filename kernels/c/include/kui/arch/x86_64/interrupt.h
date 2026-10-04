/*
 * GDT / TSS / IDT と、割り込み・例外の受け付け（x86_64）。
 *
 * 割り込みベクタ（0〜255）の割り当て:
 *   0x00〜0x1f : CPU の例外（#DE, #PF など）
 *   0x20〜0x2f : ISA の IRQ 0〜15（I/O APIC 経由。番号 = 0x20 + IRQ）
 *   0x40       : Local APIC タイマー
 *   0x81       : カーネル内テスト用（ソフトウェア割り込みで呼ぶ）
 *   0xff       : Local APIC のスプリアス割り込み（何もしない）
 */
#ifndef KUI_ARCH_X86_64_INTERRUPT_H
#define KUI_ARCH_X86_64_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

#define VEC_EXCEPTION_COUNT 32
#define VEC_IRQ_BASE        0x20
#define VEC_LAPIC_TIMER     0x40
#define VEC_KTEST           0x81
#define VEC_SPURIOUS        0xff

/* 例外ベクタの番号 */
#define VEC_DE  0  /* Divide Error */
#define VEC_NMI 2  /* Non-Maskable Interrupt */
#define VEC_BP  3  /* Breakpoint */
#define VEC_UD 6  /* Invalid Opcode */
#define VEC_DF 8  /* Double Fault */
#define VEC_GP 13 /* General Protection */
#define VEC_PF 14 /* Page Fault */
#define VEC_MC 18 /* Machine Check */

/*
 * 割り込み発生時に保存されるレジスタ。アセンブリの入口（isr.S）が
 * 汎用レジスタを push し、ベクタ番号とエラーコードを積んでから C へ渡す。
 * 下半分（rip 以降）は CPU 自身が積む。並び順を変えるときは isr.S も直すこと。
 */
struct interrupt_frame {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t vector;
	uint64_t error_code; /* エラーコードのない例外・割り込みでは 0 */
	uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*interrupt_handler)(struct interrupt_frame *frame);

/* GDT と TSS を作って読み込み、セグメントレジスタを読み直す */
void gdt_init(void);

/* IDT を作って読み込む。全 256 ベクタの入口を登録する */
void idt_init(void);

/*
 * ベクタ vector の処理関数を登録する（NULL で解除）。
 * 例外ベクタに登録すると、既定の処理（レジスタを表示して panic）の代わりに呼ばれる。
 * 外部割り込みの処理関数は、最後に lapic_eoi() を呼ぶこと。
 */
void interrupt_register(uint8_t vector, interrupt_handler handler);

/* isr.S から呼ばれる共通の振り分け処理 */
void interrupt_dispatch(struct interrupt_frame *frame);

/* frame の内容（レジスタ、CR2 など）を表示する。例外の報告で使う */
void interrupt_frame_dump(const struct interrupt_frame *frame);

/* 例外ベクタの名前（"Page Fault" など）と略号（"#PF" など）。範囲外なら "?" */
const char *exception_name(uint64_t vector);
const char *exception_mnemonic(uint64_t vector);

/*
 * CPU 関連の自己テスト（コマンドラインの selftest=...）。
 * 名前を知っていれば実行して true（多くは panic して戻らない）、知らなければ false。
 */
bool cpu_selftest(const char *name);

#endif
