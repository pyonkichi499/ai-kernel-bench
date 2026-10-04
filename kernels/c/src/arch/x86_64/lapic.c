/*
 * Local APIC（CPU ごとの割り込みコントローラ）。
 *
 * レジスタへのアクセス方法は 2 通りある:
 *   xAPIC  : 物理アドレス（通常 0xfee00000）に置かれた MMIO を読み書きする
 *   x2APIC : MSR（0x800 + レジスタのオフセット / 16）を rdmsr / wrmsr で読み書きする
 * ファームウェアが x2APIC モードにしている場合、xAPIC の MMIO は使えないので、
 * IA32_APIC_BASE MSR の bit 10 を見てどちらで読み書きするかを決める。
 */
#include <stdbool.h>
#include <stdint.h>

#include <kui/arch/x86_64/apic.h>
#include <kui/arch/x86_64/cpu.h>
#include <kui/mmio.h>
#include <kui/panic.h>
#include <kui/printk.h>

#define MSR_APIC_BASE        0x1b
#define APIC_BASE_X2APIC     (1ULL << 10) /* x2APIC モード */
#define APIC_BASE_ENABLE     (1ULL << 11) /* Local APIC 全体の有効化 */
#define APIC_BASE_ADDR_MASK  0x000ffffffffff000ULL
#define MSR_X2APIC_BASE      0x800

/* レジスタのオフセット（xAPIC の MMIO 上の位置） */
#define LAPIC_ID          0x020
#define LAPIC_TPR         0x080 /* Task Priority Register */
#define LAPIC_EOI         0x0b0
#define LAPIC_SVR         0x0f0 /* Spurious Interrupt Vector Register */
#define LAPIC_LVT_TIMER   0x320
#define LAPIC_TIMER_INIT  0x380 /* 初期値 */
#define LAPIC_TIMER_CUR   0x390 /* 現在値 */
#define LAPIC_TIMER_DIV   0x3e0 /* 分周設定 */

#define SVR_ENABLE        (1u << 8) /* ソフトウェア的な有効化 */
#define LVT_MASKED        (1u << 16)
#define LVT_TIMER_PERIODIC (1u << 17)

static volatile void *lapic_mmio;
static bool x2apic_mode;
/* lapic_init() が済んだか。済む前の lapic_eoi() は何もしない（MMIO が未割り当てのため） */
static bool lapic_ready;

static uint32_t lapic_read(uint32_t reg)
{
	if (x2apic_mode)
		return (uint32_t)cpu_rdmsr(MSR_X2APIC_BASE + (reg >> 4));
	return mmio_read32(lapic_mmio, reg);
}

static void lapic_write(uint32_t reg, uint32_t value)
{
	if (x2apic_mode)
		cpu_wrmsr(MSR_X2APIC_BASE + (reg >> 4), value);
	else
		mmio_write32(lapic_mmio, reg, value);
}

void lapic_init(uint64_t lapic_phys)
{
	uint64_t base = cpu_rdmsr(MSR_APIC_BASE);
	uint64_t msr_phys = base & APIC_BASE_ADDR_MASK;

	if (!(base & APIC_BASE_ENABLE)) {
		/* Limine（base revision 5 以降）は有効にしてから渡すはずだが、念のため有効にする */
		base |= APIC_BASE_ENABLE;
		cpu_wrmsr(MSR_APIC_BASE, base);
	}
	x2apic_mode = (base & APIC_BASE_X2APIC) != 0;

	/*
	 * MADT の値と MSR の値が食い違うことは通常ないが、実際に CPU が使っているのは
	 * MSR の値なので、そちらを優先する。
	 */
	if (lapic_phys == 0 || lapic_phys != msr_phys) {
		if (lapic_phys != 0)
			printk("lapic: MADT base 0x%llx differs from MSR 0x%llx, using MSR\n",
			       (unsigned long long)lapic_phys, (unsigned long long)msr_phys);
		lapic_phys = msr_phys;
	}
	if (!x2apic_mode)
		lapic_mmio = mmio_map(lapic_phys, 0x1000);

	/* 優先度 0（すべての割り込みを受け付ける）、スプリアスベクタ 0xff で有効化 */
	lapic_write(LAPIC_TPR, 0);
	lapic_write(LAPIC_SVR, SVR_ENABLE | 0xff);
	/* タイマーは使い始めるまで止めておく */
	lapic_timer_stop();

	lapic_ready = true;
	printk("lapic: id %u, base 0x%llx%s\n", lapic_id(), (unsigned long long)lapic_phys,
	       x2apic_mode ? " (x2apic)" : "");
}

uint32_t lapic_id(void)
{
	uint32_t id = lapic_read(LAPIC_ID);

	/* xAPIC では上位 8 ビットが ID、x2APIC では 32 ビット全体が ID */
	return x2apic_mode ? id : id >> 24;
}

void lapic_eoi(void)
{
	if (!lapic_ready)
		return;
	lapic_write(LAPIC_EOI, 0);
}

/*
 * 分周設定の値。2^shift で割る（shift = 0〜7 で 1, 2, 4, ..., 128 分周）。
 * レジスタの値は bit 0,1,3 に分かれていて並びが不規則なので表で引く。
 */
static uint32_t divide_value(uint8_t divide_shift)
{
	static const uint8_t table[8] = {0xb, 0x0, 0x1, 0x2, 0x3, 0x8, 0x9, 0xa};

	if (divide_shift > 7)
		panic("lapic: bad timer divide shift %u", (unsigned)divide_shift);
	return table[divide_shift];
}

void lapic_timer_start_periodic(uint8_t vector, uint32_t initial_count, uint8_t divide_shift)
{
	lapic_write(LAPIC_TIMER_DIV, divide_value(divide_shift));
	lapic_write(LAPIC_LVT_TIMER, vector | LVT_TIMER_PERIODIC);
	/* 初期値を書いた時点で数え始める */
	lapic_write(LAPIC_TIMER_INIT, initial_count);
}

void lapic_timer_stop(void)
{
	lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
	lapic_write(LAPIC_TIMER_INIT, 0);
}

uint32_t lapic_timer_current(void)
{
	return lapic_read(LAPIC_TIMER_CUR);
}

void lapic_timer_start_oneshot_max(uint8_t divide_shift)
{
	/* 割り込みはマスクしたまま、ワンショットで最大値から数え下げる */
	lapic_write(LAPIC_TIMER_DIV, divide_value(divide_shift));
	lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
	lapic_write(LAPIC_TIMER_INIT, 0xffffffffu);
}
