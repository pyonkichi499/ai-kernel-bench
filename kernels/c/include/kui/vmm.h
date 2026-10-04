/*
 * 仮想メモリ管理（Virtual Memory Manager）: x86_64 の 4 段ページテーブル。
 *
 * カーネル用のページテーブルを自前で作り、Limine の仮のページテーブルから切り替える。
 * 仮想アドレス空間の配置:
 *   0x0000000000000000〜0x00007fffffffffff : ユーザー空間（M4 以降、プロセスごと）
 *   0xffff800000000000〜                    : 物理メモリの直接マップ（HHDM。Limine と同じ位置）
 *   0xffffc00000000000〜                    : MMIO の割り当て領域
 *   0xffffffff80000000〜                    : カーネル本体（text: R-X、rodata: R--、data/bss: RW-）
 *
 * 上半分（カーネル側）の PML4 エントリは全アドレス空間で共有する（M4 でプロセスを作るとき、
 * カーネル側の 256 エントリをコピーするだけでカーネルが見えるようにするため）。
 */
#ifndef KUI_VMM_H
#define KUI_VMM_H

#include <stdbool.h>
#include <stdint.h>

#include <kui/boot_info.h>

#define VMM_HHDM_BASE 0xffff800000000000ULL
#define VMM_MMIO_BASE 0xffffc00000000000ULL
#define VMM_MMIO_SIZE 0x0000004000000000ULL /* 256GiB */

/* 割り当ての属性 */
#define VMM_WRITE   0x1 /* 書き込み可 */
#define VMM_EXEC    0x2 /* 実行可（なければ NX） */
#define VMM_USER    0x4 /* ユーザーモードから触れる */
#define VMM_NOCACHE 0x8 /* キャッシュ無効（MMIO 用） */

struct address_space {
	uint64_t pml4_phys; /* CR3 に入れる物理アドレス */
};

/*
 * カーネルのページテーブルを作り、CR3 を切り替える。
 * 表示: "vmm: kernel page tables active (pml4 0x<phys>)"
 */
void vmm_init(const struct boot_info *info);

/* カーネルのアドレス空間 */
struct address_space *vmm_kernel_space(void);

/* virt（4KiB 境界）に phys を 1 ページ割り当てる。途中のテーブルは必要なら確保する */
bool vmm_map_page(struct address_space *as, uint64_t virt, uint64_t phys, unsigned flags);
/* 割り当てを外し、外したページの物理アドレスを返す（なければ 0） */
uint64_t vmm_unmap_page(struct address_space *as, uint64_t virt);
/* virt に対応する物理アドレス。割り当てがなければ false */
bool vmm_translate(struct address_space *as, uint64_t virt, uint64_t *phys_out,
		   unsigned *flags_out);

/* MMIO（phys から size バイト）を MMIO 領域に割り当てて仮想アドレスを返す */
volatile void *vmm_map_mmio(uint64_t phys, uint64_t size);

/* 物理アドレスと直接マップ上の仮想アドレスの変換 */
static inline void *phys_to_virt(uint64_t phys)
{
	return (void *)(phys + VMM_HHDM_BASE);
}

static inline uint64_t virt_to_phys_hhdm(const void *virt)
{
	return (uint64_t)virt - VMM_HHDM_BASE;
}

#endif
