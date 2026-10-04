/* CPU を直接操作する小さな関数群（x86_64） */
#ifndef KUI_ARCH_X86_64_CPU_H
#define KUI_ARCH_X86_64_CPU_H

#include <stdbool.h>
#include <stdint.h>

/* RFLAGS の IF ビット（1 なら割り込み許可） */
#define RFLAGS_IF (1ULL << 9)

/* 割り込みを禁止する */
static inline void cpu_disable_interrupts(void)
{
	__asm__ volatile("cli" : : : "memory");
}

/* 割り込みを許可する */
static inline void cpu_enable_interrupts(void)
{
	__asm__ volatile("sti" : : : "memory");
}

static inline uint64_t cpu_read_rflags(void)
{
	uint64_t flags;

	__asm__ volatile("pushfq; popq %0" : "=r"(flags) : : "memory");
	return flags;
}

static inline bool cpu_interrupts_enabled(void)
{
	return (cpu_read_rflags() & RFLAGS_IF) != 0;
}

/*
 * 割り込みを禁止し、禁止する前の RFLAGS を返す。
 * cpu_irq_restore() と組で使い、「元々禁止されていたなら禁止のまま」を保つ。
 */
static inline uint64_t cpu_irq_save(void)
{
	uint64_t flags = cpu_read_rflags();

	cpu_disable_interrupts();
	return flags;
}

static inline void cpu_irq_restore(uint64_t flags)
{
	if (flags & RFLAGS_IF)
		cpu_enable_interrupts();
}

/* 割り込みが来るまで CPU を休ませる（割り込み許可状態で呼ぶこと） */
static inline void cpu_halt(void)
{
	__asm__ volatile("hlt" : : : "memory");
}

/* スピンループ中であることを CPU に伝える（消費電力と性能のため） */
static inline void cpu_pause(void)
{
	__asm__ volatile("pause" : : : "memory");
}

/* 割り込みを禁止したうえで、CPU を永久に停止させる */
_Noreturn static inline void cpu_halt_forever(void)
{
	cpu_disable_interrupts();
	for (;;)
		__asm__ volatile("hlt");
}

static inline uint64_t cpu_rdmsr(uint32_t msr)
{
	uint32_t lo, hi;

	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((uint64_t)hi << 32) | lo;
}

static inline void cpu_wrmsr(uint32_t msr, uint64_t value)
{
	__asm__ volatile("wrmsr"
			 :
			 : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32))
			 : "memory");
}

static inline uint64_t cpu_read_cr2(void)
{
	uint64_t v;

	__asm__ volatile("mov %%cr2, %0" : "=r"(v));
	return v;
}

static inline uint64_t cpu_read_cr3(void)
{
	uint64_t v;

	__asm__ volatile("mov %%cr3, %0" : "=r"(v));
	return v;
}

/* 仮想アドレス addr の TLB（ページテーブルのキャッシュ）を無効化する */
static inline void cpu_invlpg(uint64_t addr)
{
	__asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline uint64_t cpu_rdtsc(void)
{
	uint32_t lo, hi;

	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

#endif
