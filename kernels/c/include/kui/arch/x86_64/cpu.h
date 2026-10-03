/* CPU を直接操作する小さな関数群（x86_64） */
#ifndef KUI_ARCH_X86_64_CPU_H
#define KUI_ARCH_X86_64_CPU_H

/* 割り込みを禁止する */
static inline void cpu_disable_interrupts(void)
{
	__asm__ volatile("cli" : : : "memory");
}

/* 割り込みを禁止したうえで、CPU を永久に停止させる */
_Noreturn static inline void cpu_halt_forever(void)
{
	cpu_disable_interrupts();
	for (;;)
		__asm__ volatile("hlt");
}

#endif
