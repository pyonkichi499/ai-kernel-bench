/*
 * GDT / TSS / IDT、割り込みの振り分け、シンボル表のカーネル内テスト。
 */
#include <stdint.h>

#include <kui/arch/x86_64/gdt.h>
#include <kui/arch/x86_64/interrupt.h>
#include <kui/boot_info.h>
#include <kui/ktest.h>
#include <kui/stacktrace.h>
#include <kui/string.h>

struct __attribute__((packed)) descriptor_table_register {
	uint16_t limit;
	uint64_t base;
};

static volatile int ktest_vector_calls;
static volatile uint64_t ktest_vector_seen;
static volatile uint64_t ktest_vector_rip;
static volatile uint64_t ktest_vector_cs;

static void ktest_vector_handler(struct interrupt_frame *frame)
{
	ktest_vector_calls++;
	ktest_vector_seen = frame->vector;
	ktest_vector_rip = frame->rip;
	ktest_vector_cs = frame->cs;
}

KTEST(interrupt_software_vector_dispatches_to_handler)
{
	uint64_t offset = 0;
	const char *name;

	ktest_vector_calls = 0;
	interrupt_register(VEC_KTEST, ktest_vector_handler);
	__asm__ volatile("int $0x81" : : : "memory");
	interrupt_register(VEC_KTEST, 0);

	KEXPECT_EQ(ktest_vector_calls, 1);
	KEXPECT_EQ(ktest_vector_seen, VEC_KTEST);
	KEXPECT_EQ(ktest_vector_cs, GDT_SEL_KERNEL_CODE);
	/* 戻り先（int 命令の次）はこのテスト関数の中にある */
	name = symbol_lookup(ktest_vector_rip, &offset);
	KEXPECT(name != 0);
	if (name)
		KEXPECT_STREQ(name, "ktest_fn_interrupt_software_vector_dispatches_to_handler");
}

KTEST(interrupt_unregistered_handler_is_not_called)
{
	ktest_vector_calls = 0;
	interrupt_register(VEC_KTEST, ktest_vector_handler);
	interrupt_register(VEC_KTEST, 0);
	/* 未登録の外部割り込みは「unexpected vector」を表示するだけで戻る */
	__asm__ volatile("int $0x81" : : : "memory");
	KEXPECT_EQ(ktest_vector_calls, 0);
}

static volatile int ktest_bp_calls;

static void ktest_bp_handler(struct interrupt_frame *frame)
{
	(void)frame;
	ktest_bp_calls++;
}

KTEST(interrupt_breakpoint_returns)
{
	ktest_bp_calls = 0;
	interrupt_register(VEC_BP, ktest_bp_handler);
	__asm__ volatile("int3" : : : "memory");
	__asm__ volatile("int3" : : : "memory");
	interrupt_register(VEC_BP, 0);
	KEXPECT_EQ(ktest_bp_calls, 2);

	/* 処理関数がなくても、#BP の既定の処理は報告して戻ってくる */
	__asm__ volatile("int3" : : : "memory");
	KEXPECT_EQ(ktest_bp_calls, 2);
}

KTEST(interrupt_registers_survive_interrupt)
{
	/* 割り込みの前後で、呼び出し先保存レジスタの値が保たれること（isr.S の push/pop の対応） */
	uint64_t rbx = 0x1111222233334444ULL, r12 = 0x5555666677778888ULL;
	uint64_t r13 = 0x9999aaaabbbbccccULL, r14 = 0xddddeeeeffff0000ULL;
	uint64_t rbx_after, r12_after, r13_after, r14_after;

	interrupt_register(VEC_KTEST, ktest_vector_handler);
	__asm__ volatile("movq %4, %%rbx\n\t"
			 "movq %5, %%r12\n\t"
			 "movq %6, %%r13\n\t"
			 "movq %7, %%r14\n\t"
			 "int $0x81\n\t"
			 "movq %%rbx, %0\n\t"
			 "movq %%r12, %1\n\t"
			 "movq %%r13, %2\n\t"
			 "movq %%r14, %3"
			 : "=m"(rbx_after), "=m"(r12_after), "=m"(r13_after), "=m"(r14_after)
			 : "r"(rbx), "r"(r12), "r"(r13), "r"(r14)
			 : "rbx", "r12", "r13", "r14", "memory");
	interrupt_register(VEC_KTEST, 0);

	KEXPECT_EQ(rbx_after, rbx);
	KEXPECT_EQ(r12_after, r12);
	KEXPECT_EQ(r13_after, r13);
	KEXPECT_EQ(r14_after, r14);
}

KTEST(exception_names_table)
{
	KEXPECT_STREQ(exception_name(VEC_DE), "Divide Error");
	KEXPECT_STREQ(exception_name(VEC_PF), "Page Fault");
	KEXPECT_STREQ(exception_name(VEC_DF), "Double Fault");
	KEXPECT_STREQ(exception_mnemonic(VEC_PF), "#PF");
	KEXPECT_STREQ(exception_mnemonic(VEC_GP), "#GP");
	KEXPECT_STREQ(exception_mnemonic(VEC_UD), "#UD");
	KEXPECT_STREQ(exception_name(200), "?");
	KEXPECT_STREQ(exception_mnemonic(32), "?");
}

KTEST(symbol_lookup_finds_known_functions)
{
	uint64_t offset = 1234;
	uint64_t kmain_addr = (uint64_t)(uintptr_t)&kmain;
	const char *name;

	name = symbol_lookup(kmain_addr, &offset);
	KEXPECT(name != 0);
	if (name)
		KEXPECT_STREQ(name, "kmain");
	KEXPECT_EQ(offset, 0);

	name = symbol_lookup(kmain_addr + 3, &offset);
	KEXPECT(name != 0);
	if (name)
		KEXPECT_STREQ(name, "kmain");
	KEXPECT_EQ(offset, 3);

	name = symbol_lookup((uint64_t)(uintptr_t)&symbol_lookup, &offset);
	KEXPECT(name != 0);
	if (name)
		KEXPECT_STREQ(name, "symbol_lookup");

	/* カーネルの外のアドレスは見つからない */
	KEXPECT(symbol_lookup(0, &offset) == 0);
	KEXPECT(symbol_lookup(0x1000, &offset) == 0);
}

KTEST(gdt_segment_registers_and_tss)
{
	struct descriptor_table_register gdtr;
	uint16_t cs, ds, ss, tr;
	const uint64_t *gdt;
	uint64_t low, high, tss_base;
	uint64_t ist[3];

	__asm__ volatile("sgdt %0" : "=m"(gdtr));
	__asm__ volatile("movw %%cs, %0" : "=r"(cs));
	__asm__ volatile("movw %%ds, %0" : "=r"(ds));
	__asm__ volatile("movw %%ss, %0" : "=r"(ss));
	__asm__ volatile("str %0" : "=r"(tr));

	KEXPECT_EQ(cs, GDT_SEL_KERNEL_CODE);
	KEXPECT_EQ(ds, GDT_SEL_KERNEL_DATA);
	KEXPECT_EQ(ss, GDT_SEL_KERNEL_DATA);
	KEXPECT_EQ(tr, GDT_SEL_TSS);
	/* null, kcode, kdata, udata, ucode, TSS(2 エントリ) = 7 エントリ */
	KEXPECT_EQ(gdtr.limit, 7 * 8 - 1);
	/* GDT はカーネルの中（上位 2GiB）にある。Limine の GDT のままではない */
	KEXPECT(gdtr.base >= 0xffffffff80000000ULL);

	gdt = (const uint64_t *)(uintptr_t)gdtr.base;
	/* カーネルコード: L=1（64 ビット）、DPL=0、P=1 */
	KEXPECT_EQ((gdt[1] >> 53) & 1, 1);
	KEXPECT_EQ((gdt[1] >> 45) & 3, 0);
	/* ユーザーコード: L=1、DPL=3 */
	KEXPECT_EQ((gdt[4] >> 53) & 1, 1);
	KEXPECT_EQ((gdt[4] >> 45) & 3, 3);
	/* ユーザーデータ: DPL=3 */
	KEXPECT_EQ((gdt[3] >> 45) & 3, 3);

	/* TSS 記述子から TSS の場所を復元し、IST1〜3 が設定されていること */
	low = gdt[GDT_SEL_TSS / 8];
	high = gdt[GDT_SEL_TSS / 8 + 1];
	/* 種類 0xb = 使用中の 64 ビット TSS（ltr で「使用中」になる） */
	KEXPECT_EQ((low >> 40) & 0xf, 0xb);
	tss_base = ((low >> 16) & 0xffffffULL) | (((low >> 56) & 0xffULL) << 24) | (high << 32);
	/*
	 * TSS の ist[] はオフセット 36 から始まり、8 バイト境界にそろっていない
	 * （ハードウェアの決めた形）。ポインタで直接読むとアラインメント違反になるので、
	 * memcpy でコピーしてから読む。
	 */
	memcpy(ist, (const void *)(uintptr_t)(tss_base + 36), sizeof(ist));
	for (int i = 0; i < 3; i++) {
		KEXPECT(ist[i] != 0);
		KEXPECT_EQ(ist[i] & 15, 0);
	}
	/* 3 本のスタックは別々の場所 */
	KEXPECT(ist[0] != ist[1] && ist[1] != ist[2] && ist[0] != ist[2]);
}

KTEST(idt_entries_configured)
{
	struct descriptor_table_register idtr;
	const uint8_t *idt;

	__asm__ volatile("sidt %0" : "=m"(idtr));
	KEXPECT_EQ(idtr.limit, 256 * 16 - 1);
	KEXPECT(idtr.base >= 0xffffffff80000000ULL);

	idt = (const uint8_t *)(uintptr_t)idtr.base;
	for (int v = 0; v < 256; v++) {
		const uint8_t *e = idt + v * 16;
		uint16_t selector = (uint16_t)(e[2] | (e[3] << 8));

		if (selector != GDT_SEL_KERNEL_CODE || e[5] != 0x8e) {
			KEXPECT_EQ(selector, GDT_SEL_KERNEL_CODE);
			KEXPECT_EQ(e[5], 0x8e);
			break;
		}
	}
	/* IST の割り当て: #DF=1、NMI=2、#MC=3、#PF などは 0 */
	KEXPECT_EQ(idt[VEC_DF * 16 + 4] & 7, IST_DOUBLE_FAULT);
	KEXPECT_EQ(idt[2 * 16 + 4] & 7, IST_NMI);
	KEXPECT_EQ(idt[18 * 16 + 4] & 7, IST_MACHINE_CHECK);
	KEXPECT_EQ(idt[VEC_PF * 16 + 4] & 7, 0);
}

KTEST(stacktrace_print_current_does_not_crash)
{
	/* 表示内容は目視用。ここでは途中で例外が起きず戻ってくることを確かめる */
	stacktrace_print_current();
	KEXPECT(1);
}

/*
 * 壊れたフレームポインタを渡しても、スタックトレースの表示が #PF を起こさず止まること。
 * （#PF が起きればテスト用カーネルは panic して失敗で終わるので、戻ってくれば合格）
 */
KTEST(stacktrace_rejects_unreadable_frame_pointers)
{
	const struct boot_info *info = boot_info_get();

	/* 下位半分の未割り当てアドレス */
	stacktrace_print((uint64_t)(uintptr_t)ktest_vector_handler, 0xdead0000ULL);
	/* 8 バイト境界にそろっていない */
	stacktrace_print((uint64_t)(uintptr_t)ktest_vector_handler, 0xffffffff80000004ULL);
	/* 直接マップの範囲内だが RAM のない位置（物理 64TiB） */
	stacktrace_print((uint64_t)(uintptr_t)ktest_vector_handler,
			 info->hhdm_offset + (1ULL << 46));
	/* 非正規（canonical でない）アドレス */
	stacktrace_print((uint64_t)(uintptr_t)ktest_vector_handler, 0x8000000000000000ULL);
	KEXPECT(1);
}
