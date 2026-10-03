/*
 * ブートローダーから受け取った boot_info の内容が妥当であることの確認。
 *
 * 変換層（src/boot/limine.c）の誤りは、M2 のメモリ管理で原因の分かりにくい
 * 不具合になって現れるので、ここで早めに検出する。
 */
#include <stdint.h>

#include <kui/boot_info.h>
#include <kui/ktest.h>
#include <kui/string.h>

KTEST(boot_info_basic_fields)
{
	const struct boot_info *info = boot_info_get();

	KEXPECT(info->bootloader[0] != '\0');
	KEXPECT(strncmp(info->bootloader, "Limine", 6) == 0);
	/* HHDM は上位半分（カーネル空間）にある */
	KEXPECT(info->hhdm_offset >= 0xffff800000000000ull);
	/* カーネルはリンカスクリプトで指定したアドレスに置かれている */
	KEXPECT_EQ(info->kernel_virt_base, 0xffffffff80000000ull);
	/* 物理アドレスはページ境界にそろっている */
	KEXPECT_EQ(info->kernel_phys_base & 0xfff, 0);
	/* QEMU の UEFI には ACPI がある */
	KEXPECT(info->rsdp_phys != 0);
	KEXPECT(info->rsdp_phys < info->hhdm_offset);
}

KTEST(boot_info_rsdp_signature)
{
	const struct boot_info *info = boot_info_get();
	const char *rsdp;

	if (info->rsdp_phys == 0) {
		KEXPECT(info->rsdp_phys != 0);
		return;
	}
	/* 物理アドレスを HHDM で仮想アドレスに直して読み、RSDP の署名を確かめる */
	rsdp = (const char *)(uintptr_t)(info->rsdp_phys + info->hhdm_offset);
	KEXPECT(memcmp(rsdp, "RSD PTR ", 8) == 0);
}

KTEST(boot_info_memmap_sorted_and_disjoint)
{
	const struct boot_info *info = boot_info_get();

	KEXPECT(info->memmap_count > 0);
	KEXPECT(info->memmap_count <= BOOT_INFO_MAX_MEMMAP);
	for (size_t i = 0; i < info->memmap_count; i++) {
		const struct mem_region *r = &info->memmap[i];

		KEXPECT(r->length > 0);
		KEXPECT(r->type < MEM_TYPE_COUNT);
		/* アドレスの昇順に並び、隣と重ならない（Limine の仕様で保証される） */
		if (i > 0) {
			const struct mem_region *prev = &info->memmap[i - 1];

			if (prev->base + prev->length > r->base)
				ktest_fail(ctx, __FILE__, __LINE__,
					   "memmap[%zu] overlaps or is unsorted", i);
		}
	}
}

KTEST(boot_info_memmap_has_usable_and_kernel)
{
	const struct boot_info *info = boot_info_get();
	uint64_t usable = 0;
	int kernel_found = 0;

	for (size_t i = 0; i < info->memmap_count; i++) {
		const struct mem_region *r = &info->memmap[i];

		if (r->type == MEM_USABLE) {
			usable += r->length;
			/* 使えるメモリはページ境界にそろっている */
			KEXPECT_EQ(r->base & 0xfff, 0);
			KEXPECT_EQ(r->length & 0xfff, 0);
		}
		/* カーネル本体の物理アドレスは「カーネルとモジュール」の領域の中にある */
		if (r->type == MEM_KERNEL_AND_MODULES && info->kernel_phys_base >= r->base &&
		    info->kernel_phys_base < r->base + r->length)
			kernel_found = 1;
	}
	/* QEMU は -m 256M で起動するので、少なくとも 128MiB は使えるはず */
	KEXPECT(usable >= 128ull * 1024 * 1024);
	KEXPECT(kernel_found);
}

KTEST(boot_info_mem_type_names)
{
	KEXPECT_STREQ(mem_type_name(MEM_USABLE), "usable");
	KEXPECT_STREQ(mem_type_name(MEM_KERNEL_AND_MODULES), "kernel-and-modules");
	KEXPECT_STREQ(mem_type_name(MEM_TYPE_COUNT), "unknown");
}
