/*
 * カーネル本体の入口。
 *
 * ブートローダーの変換層（src/boot/）が boot_info を用意してから kmain() を呼ぶ。
 * M0 では、受け取った情報をシリアルに表示し、必要ならコマンドラインで指定された
 * 自己テストを行ってから停止する。
 */
#include <stdint.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/boot_info.h>
#include <kui/cmdline.h>
#include <kui/panic.h>
#include <kui/printk.h>
#include <kui/string.h>

#ifdef KUI_KTEST
#include <kui/ktest.h>
#include <kui/qemu.h>
#endif

#define KUI_VERSION "0.0.1"

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

/*
 * コマンドラインの "selftest=..." で指定された自己テストを行う。
 * 異常を検出する仕組み（UBSan、panic）そのものが働くことを、結合テストで確かめるため。
 */
static void run_selftest(const char *cmdline)
{
	char name[32];

	if (!cmdline_get(cmdline, "selftest", name, sizeof(name)))
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
	} else {
		printk("selftest: unknown selftest '%s'\n", name);
	}
}

_Noreturn void kmain(const struct boot_info *info)
{
	printk("Kui " KUI_VERSION " (Tangmen C kernel)\n");
	print_boot_info(info);
	run_selftest(info->cmdline);

#ifdef KUI_KTEST
	qemu_exit(ktest_run_all() ? QEMU_EXIT_SUCCESS : QEMU_EXIT_FAILURE);
#else
	printk("kui: init complete\n");
	cpu_halt_forever();
#endif
}
