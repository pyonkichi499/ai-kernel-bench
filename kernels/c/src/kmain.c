/*
 * カーネル本体の入口。
 *
 * ブートローダーの変換層（src/boot/）が boot_info を用意してから kmain() を呼ぶ。
 *
 * 初期化の順番（M1）:
 *   1. 受け取った boot_info を表示する（M0）
 *   2. GDT / TSS → IDT: 例外を受け止められるようにする。これより後の不具合は
 *      「何の例外がどこで起きたか」を表示して止まれる
 *   3. ACPI（MADT）: 割り込みコントローラの場所を調べる
 *   4. 8259 PIC を止め、Local APIC と I/O APIC を初期化する
 *   5. Local APIC タイマーを較正して 100Hz で動かす
 *   6. 割り込みを許可する。ここで初めてタイマー割り込みが届き始める
 *   7. 自己テスト（コマンドラインの selftest=...）
 *   8. タイマー割り込みが実際に届くことを確かめる
 * その後、通常のカーネルは割り込みを待ちながら休み続ける（まだ実行するものがない）。
 */
#include <stdint.h>

#include <kui/acpi.h>
#include <kui/arch/x86_64/apic.h>
#include <kui/arch/x86_64/cpu.h>
#include <kui/arch/x86_64/interrupt.h>
#include <kui/boot_info.h>
#include <kui/cmdline.h>
#include <kui/panic.h>
#include <kui/printk.h>
#include <kui/spinlock.h>
#include <kui/string.h>
#include <kui/timer.h>

#ifdef KUI_KTEST
#include <kui/ktest.h>
#include <kui/qemu.h>
#endif

#define KUI_VERSION "0.0.1"

/* タイマー割り込みの頻度（回/秒） */
#define TIMER_HZ 100

/* 起動時に、タイマー割り込みがこの回数届くまで待って動作を確かめる */
#define TIMER_CHECK_TICKS 10

static void print_boot_info(const struct boot_info *info)
{
	uint64_t usable = 0;

	printk("bootloader: %s\n", info->bootloader);
	printk("cmdline: %s\n", info->cmdline);
	printk("hhdm: 0x%llx\n", (unsigned long long)info->hhdm_offset);
	printk("kernel: phys 0x%llx virt 0x%llx\n", (unsigned long long)info->kernel_phys_base,
	       (unsigned long long)info->kernel_virt_base);
	printk("rsdp: 0x%llx\n", (unsigned long long)info->rsdp_phys);
	if (info->framebuffer.present)
		printk("framebuffer: %llux%llu %ubpp\n",
		       (unsigned long long)info->framebuffer.width,
		       (unsigned long long)info->framebuffer.height,
		       (unsigned)info->framebuffer.bpp);
	else
		printk("framebuffer: none\n");

	for (size_t i = 0; i < info->memmap_count; i++) {
		if (info->memmap[i].type == MEM_USABLE)
			usable += info->memmap[i].length;
	}
	printk("memmap: %zu entries, %llu KiB usable\n", info->memmap_count,
	       (unsigned long long)(usable / 1024));
	for (size_t i = 0; i < info->memmap_count; i++) {
		const struct mem_region *r = &info->memmap[i];

		printk("  [0x%016llx, 0x%016llx) %s\n", (unsigned long long)r->base,
		       (unsigned long long)(r->base + r->length), mem_type_name(r->type));
	}
}

/* 割り込みコントローラとタイマーを準備する（割り込みはまだ許可しない） */
static void init_interrupts(const struct boot_info *info)
{
	const struct acpi_madt_info *madt;

	gdt_init();
	idt_init();

	if (!acpi_init(info))
		panic("acpi: MADT not found; cannot set up interrupt controllers");
	madt = acpi_madt();

	pic_disable();
	lapic_init(madt->lapic_phys);
	ioapic_init();
	timer_init(TIMER_HZ);
}

/*
 * スピンロックの二重取得を検出できることの自己テスト。
 * 割り込みを禁止して同じロックを 2 回取る。単一 CPU では永久に待つことになるので、
 * spinlock.c が検出して panic する（"PANIC: spinlock: deadlock: ..."）。
 */
static void selftest_spinlock(void)
{
	static struct spinlock lock = SPINLOCK_INIT("selftest");
	uint64_t flags;

	flags = spin_lock_irqsave(&lock);
	spin_lock_irqsave(&lock);
	/* ここには来ないはず */
	spin_unlock_irqrestore(&lock, flags);
	printk("selftest: spinlock deadlock was not detected\n");
}

/*
 * コマンドラインの "selftest=..." で指定された自己テストを行う。
 * 異常を検出する仕組み（UBSan、panic、例外処理など）そのものが働くことを、
 * 結合テストで確かめるため。CPU 関連のもの（例外など）は cpu_selftest() に任せる。
 */
static void run_selftest(const char *cmdline)
{
	char name[32];

	if (!cmdline_get(cmdline, "selftest", name, sizeof(name)))
		return;

	if (cpu_selftest(name))
		return;

	if (strcmp(name, "ubsan") == 0) {
		/*
		 * 符号付き整数の加算オーバーフロー（C では未定義動作）を意図的に起こす。
		 * volatile にしておかないと、コンパイラが計算を省いて検出されないことがある。
		 */
		volatile int32_t a = INT32_MAX;
		volatile int32_t b = 1;
		volatile int32_t c = a + b;

		(void)c;
		printk("selftest: ubsan was not triggered\n");
	} else if (strcmp(name, "panic") == 0) {
		panic("selftest");
	} else if (strcmp(name, "spinlock") == 0) {
		selftest_spinlock();
	} else {
		printk("selftest: unknown selftest '%s'\n", name);
	}
}

/* タイマー割り込みが実際に届いていることを確かめる */
static void check_timer(void)
{
	uint64_t start = timer_ticks();

	timer_wait_ticks(TIMER_CHECK_TICKS);
	printk("kui: timer ok (%llu ticks)\n", (unsigned long long)(timer_ticks() - start));
}

_Noreturn void kmain(const struct boot_info *info)
{
	printk("Kui " KUI_VERSION " (Tangmen C kernel)\n");
	print_boot_info(info);

	init_interrupts(info);
	printk("kui: interrupts enabled\n");
	cpu_enable_interrupts();

	run_selftest(info->cmdline);
	check_timer();

#ifdef KUI_KTEST
	qemu_exit(ktest_run_all() ? QEMU_EXIT_SUCCESS : QEMU_EXIT_FAILURE);
#else
	printk("kui: init complete\n");
	/* 実行するものがまだないので、割り込み（タイマー）を受けながら休み続ける */
	for (;;)
		cpu_halt();
#endif
}
