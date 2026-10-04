/*
 * CPU の例外処理の自己テスト（コマンドラインの selftest=...）。
 *
 * わざと例外を起こし、例外の報告（種類、レジスタ、スタックトレース）が正しく出ることを
 * 結合テストで確かめるためのもの。多くは panic して戻らない。
 *
 * 例外は C の未定義動作（0 除算など）として書くと、debug ビルドでは UBSan が先に
 * 検出してしまい CPU の例外まで届かない。そのため、例外を起こす命令はアセンブリで直接書く。
 */
#include <stdint.h>

#include <kui/arch/x86_64/interrupt.h>
#include <kui/printk.h>
#include <kui/string.h>

/* 下位半分の、どこにも割り当てられていないアドレス（Limine は下位半分を割り当てない） */
#define UNMAPPED_LOW_ADDR 0x00000000dead0000ULL

__attribute__((noinline)) static void selftest_divzero(void)
{
	/* 1 ÷ 0 を div 命令で直接計算する → #DE */
	__asm__ volatile("xorl %%edx, %%edx\n\t"
			 "movl $1, %%eax\n\t"
			 "xorl %%ecx, %%ecx\n\t"
			 "divl %%ecx"
			 :
			 :
			 : "rax", "rcx", "rdx", "memory");
}

__attribute__((noinline)) static void selftest_pagefault(void)
{
	uint64_t value;

	/* 割り当てのないアドレスを読む → #PF（CR2 にこのアドレスが入る） */
	__asm__ volatile("movq (%1), %0" : "=r"(value) : "r"(UNMAPPED_LOW_ADDR) : "memory");
	(void)value;
}

__attribute__((noinline, noreturn)) static void selftest_doublefault(void)
{
	/*
	 * スタックポインタを割り当てのないアドレスに変えてから push する。
	 *   1. push の書き込みで #PF が起きる
	 *   2. CPU は #PF の情報を（壊れた）同じスタックに積もうとして、また失敗する
	 *   3. 例外の処理中の例外なので #DF（ダブルフォールト）になる
	 * #DF は IST1 の独立したスタックで受けるので、報告まで進める。
	 * IST がなければ #DF も積めず、トリプルフォールトで CPU がリセットされる。
	 *
	 * 使うアドレスは #PF の自己テストと同じ下位半分の未割り当てアドレス。上位半分の
	 * 「今は空いている」アドレスは、M2 以降に MMIO などで割り当てられる可能性があるので使わない。
	 * カーネルは下位半分を割り当てない（ユーザー空間用。M4 以降もカーネル単独の
	 * アドレス空間では空いている）。
	 */
	__asm__ volatile("movq %0, %%rsp\n\t"
			 "pushq $0"
			 :
			 : "r"(UNMAPPED_LOW_ADDR)
			 : "memory");
	__builtin_unreachable();
}

__attribute__((noinline)) static void selftest_breakpoint(void)
{
	/* int3 → #BP。既定の処理は報告だけして、次の命令から再開する */
	__asm__ volatile("int3" : : : "memory");
	printk("selftest: breakpoint resumed\n");
}

__attribute__((noinline)) static void selftest_ud(void)
{
	/* ud2 は「未定義命令」として予約された命令 → #UD */
	__asm__ volatile("ud2" : : : "memory");
}

bool cpu_selftest(const char *name)
{
	if (strcmp(name, "divzero") == 0)
		selftest_divzero();
	else if (strcmp(name, "pagefault") == 0)
		selftest_pagefault();
	else if (strcmp(name, "doublefault") == 0)
		selftest_doublefault();
	else if (strcmp(name, "breakpoint") == 0)
		selftest_breakpoint();
	else if (strcmp(name, "ud") == 0)
		selftest_ud();
	else
		return false;

	/* 例外が起きていれば panic しているので、ここに来るのは breakpoint だけのはず */
	return true;
}
