/*
 * IDT（Interrupt Descriptor Table）と、割り込み・例外の振り分け。
 *
 * IDT は「ベクタ番号 → 入口のアドレス」の表。256 個のエントリすべてに、
 * isr.S のスタブを登録する。スタブはベクタ番号を積んで interrupt_dispatch() を呼ぶので、
 * どのベクタも最終的にここへ集まる。ここで、登録された処理関数を呼ぶか、
 * 既定の処理（例外なら報告して panic）を行う。
 */
#include <stddef.h>
#include <stdint.h>

#include <kui/arch/x86_64/apic.h>
#include <kui/arch/x86_64/cpu.h>
#include <kui/arch/x86_64/gdt.h>
#include <kui/arch/x86_64/interrupt.h>
#include <kui/panic.h>
#include <kui/printk.h>
#include <kui/stacktrace.h>

#define IDT_ENTRIES      256
#define ISR_STUB_STRIDE  16

/*
 * 64 ビットモードの IDT エントリ（16 バイト）。
 *   type_attr: 0x8e = P | DPL 0 | 種類 0xe（64 ビット割り込みゲート）
 * 割り込みゲートは、入るときに IF を 0 にする（＝処理中は割り込みを禁止する）。
 * トラップゲート（種類 0xf）は IF を変えないが、処理中に別の割り込みが入ると
 * 扱いが難しくなるので、すべて割り込みゲートにしている。
 */
struct __attribute__((packed)) idt_entry {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist; /* 下位 3 ビット: 使う IST の番号（0 なら切り替えない） */
	uint8_t type_attr;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t reserved;
};

_Static_assert(sizeof(struct idt_entry) == 16, "64 ビット IDT エントリは 16 バイト");
/* isr.S が積む形と一致していること: 汎用レジスタ 15 個 + ベクタ + エラーコード + CPU が積む 5 個 */
_Static_assert(sizeof(struct interrupt_frame) == 22 * 8, "isr.S の積み方と struct interrupt_frame");
_Static_assert(offsetof(struct interrupt_frame, vector) == 15 * 8,
	       "isr.S の積み方と vector の位置");
_Static_assert(offsetof(struct interrupt_frame, rip) == 17 * 8, "CPU が積む部分の先頭は rip");

struct __attribute__((packed)) idtr {
	uint16_t limit;
	uint64_t base;
};

#define IDT_TYPE_INTERRUPT_GATE 0x8e

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static interrupt_handler handlers[IDT_ENTRIES];

/* isr.S の 256 個のスタブの先頭と末尾 */
extern const char isr_stubs_start[];
extern const char isr_stubs_end[];

static const char *const exception_names[VEC_EXCEPTION_COUNT] = {
	"Divide Error",
	"Debug",
	"Non-Maskable Interrupt",
	"Breakpoint",
	"Overflow",
	"BOUND Range Exceeded",
	"Invalid Opcode",
	"Device Not Available",
	"Double Fault",
	"Coprocessor Segment Overrun",
	"Invalid TSS",
	"Segment Not Present",
	"Stack-Segment Fault",
	"General Protection",
	"Page Fault",
	"Reserved",
	"x87 Floating-Point Error",
	"Alignment Check",
	"Machine Check",
	"SIMD Floating-Point Exception",
	"Virtualization Exception",
	"Control Protection Exception",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Hypervisor Injection Exception",
	"VMM Communication Exception",
	"Security Exception",
	"Reserved",
};

static const char *const exception_mnemonics[VEC_EXCEPTION_COUNT] = {
	"#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM", "#DF", "#CSO", "#TS",
	"#NP", "#SS", "#GP", "#PF", "#15", "#MF", "#AC", "#MC", "#XM", "#VE",  "#CP",
	"#22", "#23", "#24", "#25", "#26", "#27", "#HV", "#VC", "#SX", "#31",
};

const char *exception_name(uint64_t vector)
{
	return vector < VEC_EXCEPTION_COUNT ? exception_names[vector] : "?";
}

const char *exception_mnemonic(uint64_t vector)
{
	return vector < VEC_EXCEPTION_COUNT ? exception_mnemonics[vector] : "?";
}

static void idt_set_entry(uint8_t vector, uint64_t handler, uint8_t ist)
{
	struct idt_entry *e = &idt[vector];

	e->offset_low = handler & 0xffff;
	e->selector = GDT_SEL_KERNEL_CODE;
	e->ist = ist & 0x7;
	e->type_attr = IDT_TYPE_INTERRUPT_GATE;
	e->offset_mid = (handler >> 16) & 0xffff;
	e->offset_high = (uint32_t)(handler >> 32);
	e->reserved = 0;
}

void idt_init(void)
{
	struct idtr idtr;

	/* isr.S のスタブが想定どおり 16 バイト間隔で 256 個並んでいるか */
	KASSERT(isr_stubs_end - isr_stubs_start == IDT_ENTRIES * ISR_STUB_STRIDE);

	for (int v = 0; v < IDT_ENTRIES; v++) {
		uint64_t stub = (uint64_t)(uintptr_t)(isr_stubs_start + v * ISR_STUB_STRIDE);

		idt_set_entry((uint8_t)v, stub, 0);
	}
	/* 通常のスタックが信用できない例外は、独立したスタック（IST）で受ける（gdt.c 参照） */
	idt[VEC_DF].ist = IST_DOUBLE_FAULT;
	idt[VEC_NMI].ist = IST_NMI;
	idt[VEC_MC].ist = IST_MACHINE_CHECK;

	idtr.limit = sizeof(idt) - 1;
	idtr.base = (uint64_t)(uintptr_t)idt;
	__asm__ volatile("lidt %0" : : "m"(idtr) : "memory");

	printk("idt: loaded (%d vectors)\n", IDT_ENTRIES);
}

void interrupt_register(uint8_t vector, interrupt_handler handler)
{
	/*
	 * ポインタ 1 個の書き込みは x86_64 では途中の状態が見えない（アトミック）。
	 * 登録の途中で割り込みが来ても、古いか新しいかのどちらかが呼ばれる。
	 */
	__atomic_store_n(&handlers[vector], handler, __ATOMIC_RELEASE);
}

void interrupt_frame_dump(const struct interrupt_frame *f)
{
	printk("  RIP=0x%016llx RSP=0x%016llx RFLAGS=0x%016llx\n", (unsigned long long)f->rip,
	       (unsigned long long)f->rsp, (unsigned long long)f->rflags);
	printk("  RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx\n", (unsigned long long)f->rax,
	       (unsigned long long)f->rbx, (unsigned long long)f->rcx);
	printk("  RDX=0x%016llx RSI=0x%016llx RDI=0x%016llx\n", (unsigned long long)f->rdx,
	       (unsigned long long)f->rsi, (unsigned long long)f->rdi);
	printk("  RBP=0x%016llx R8 =0x%016llx R9 =0x%016llx\n", (unsigned long long)f->rbp,
	       (unsigned long long)f->r8, (unsigned long long)f->r9);
	printk("  R10=0x%016llx R11=0x%016llx R12=0x%016llx\n", (unsigned long long)f->r10,
	       (unsigned long long)f->r11, (unsigned long long)f->r12);
	printk("  R13=0x%016llx R14=0x%016llx R15=0x%016llx\n", (unsigned long long)f->r13,
	       (unsigned long long)f->r14, (unsigned long long)f->r15);
	printk("  CS=0x%llx SS=0x%llx CR3=0x%016llx\n", (unsigned long long)f->cs,
	       (unsigned long long)f->ss, (unsigned long long)cpu_read_cr3());
	/* #PF では、アクセスしようとしたアドレスが CR2 に入っている */
	if (f->vector == VEC_PF)
		printk("  CR2=0x%016llx\n", (unsigned long long)cpu_read_cr2());
}

static void print_exception_header(const struct interrupt_frame *f)
{
	printk("EXCEPTION: %s %s (vector %llu, error 0x%llx)\n", exception_mnemonic(f->vector),
	       exception_name(f->vector), (unsigned long long)f->vector,
	       (unsigned long long)f->error_code);
}

/* 処理関数が登録されていない例外の既定の処理 */
static void handle_exception(struct interrupt_frame *f)
{
	/*
	 * #BP（int3）は「ここで止まって様子を見たい」という意図で置かれる命令で、
	 * 戻れば次の命令から続けられる（トラップ）。既定では報告だけして続行する。
	 */
	if (f->vector == VEC_BP) {
		print_exception_header(f);
		printk("  RIP=0x%016llx (resuming)\n", (unsigned long long)f->rip);
		return;
	}

	print_exception_header(f);
	interrupt_frame_dump(f);
	printk("  stack trace:\n");
	stacktrace_print(f->rip, f->rbp);
	panic("unhandled exception %s", exception_mnemonic(f->vector));
}

void interrupt_dispatch(struct interrupt_frame *frame)
{
	uint64_t vector = frame->vector;
	interrupt_handler handler;

	if (vector >= IDT_ENTRIES) {
		/* isr.S が積む値なので起こらないはず。起きたらフレームの形が壊れている */
		panic("interrupt: corrupt frame (vector %llu)", (unsigned long long)vector);
	}

	handler = __atomic_load_n(&handlers[vector], __ATOMIC_ACQUIRE);
	if (handler) {
		handler(frame);
		return;
	}

	if (vector < VEC_EXCEPTION_COUNT) {
		handle_exception(frame);
		return;
	}

	/* Local APIC のスプリアス割り込み。EOI も不要で、何もしなくてよい */
	if (vector == VEC_SPURIOUS)
		return;

	/*
	 * 処理関数のない外部割り込み。デバイスの設定ミスなどで起こりうるが、致命的ではない。
	 * 8259 PIC は全 IRQ をマスクし、LINT0 の ExtINT も Limine がマスクしているので、
	 * ここへ来る外部割り込みは Local APIC 経由のものだけ。EOI を送らないと、
	 * これより優先度の低い割り込みがすべて止まってしまうため、EOI を送る。
	 * （lapic_init() 前なら lapic_eoi() は何もしない）
	 */
	printk("interrupt: unexpected vector %llu\n", (unsigned long long)vector);
	lapic_eoi();
}
