/*
 * MMIO の割り当て（仕様は kui/mmio.h を参照）。
 *
 * x86_64 のページテーブルは 4 段（5 段ページングなら 5 段）の木構造で、
 * 仮想アドレスを 9 ビットずつ区切った値で各段の表を引き、最後の段（PT）で
 * 4KiB ページの物理アドレスに行き着く。途中の段の項目に PS ビットが立っていれば、
 * そこで 2MiB（PD の段）や 1GiB（PDPT の段）の大きいページとして終わる。
 *
 * ここでは Limine が作ったページテーブルを、HHDM（物理メモリの直接マップ）経由で
 * 書き換える。Limine のページテーブルはブートローダー用メモリにあり、HHDM に
 * 割り当て済みなので読み書きできる（PROTOCOL.md「Memory Layout at Entry」）。
 *
 * これは M2 で自前のページテーブルを作るまでのつなぎの仕組み。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/boot_info.h>
#include <kui/mmio.h>
#include <kui/panic.h>
#include <kui/spinlock.h>

#define PAGE_SIZE 4096ULL

/* ページテーブルの項目のビット */
#define PTE_P       (1ULL << 0)  /* 存在する */
#define PTE_RW      (1ULL << 1)  /* 書き込み可 */
#define PTE_PWT     (1ULL << 3)  /* ライトスルー */
#define PTE_PCD     (1ULL << 4)  /* キャッシュ無効 */
#define PTE_PS      (1ULL << 7)  /* 大きいページ（PD / PDPT の段のみ） */
#define PTE_PAT_4K  (1ULL << 7)  /* 4KiB ページの PAT ビット（PT の段では bit 7） */
#define PTE_PAT_BIG (1ULL << 12) /* 大きいページの PAT ビット */
#define PTE_NX      (1ULL << 63) /* 実行不可 */

/* 項目の中の物理アドレスの部分（bit 12〜51） */
#define PTE_ADDR_MASK 0x000ffffffffff000ULL

#define MSR_EFER      0xc0000080
#define EFER_NXE      (1ULL << 11)
#define CR4_LA57      (1ULL << 12)

/*
 * 途中の段のページテーブルを新しく作るための予備ページ。
 * カーネルの .bss にあるので、物理アドレスはカーネルの読み込み位置から計算できる。
 * MMIO の割り当ては Local APIC と I/O APIC の数ページだけなので 16 ページで足りる。
 */
#define SPARE_PAGES 16
static uint64_t spare_tables[SPARE_PAGES][512] __attribute__((aligned(4096)));
static size_t spare_used;

static struct spinlock mmio_lock = SPINLOCK_INIT("mmio");

void *phys_to_virt(uint64_t phys)
{
	return (void *)(uintptr_t)(phys + boot_info_get()->hhdm_offset);
}

/* カーネル本体（.bss を含む）の仮想アドレスを物理アドレスに変換する */
static uint64_t kernel_virt_to_phys(const void *virt)
{
	const struct boot_info *info = boot_info_get();

	return (uint64_t)(uintptr_t)virt - info->kernel_virt_base + info->kernel_phys_base;
}

static uint64_t *table_virt(uint64_t entry)
{
	return phys_to_virt(entry & PTE_ADDR_MASK);
}

static uint64_t read_cr4(void)
{
	uint64_t v;

	__asm__ volatile("mov %%cr4, %0" : "=r"(v));
	return v;
}

/* 予備ページを 1 枚取り出して 0 で埋め、その物理アドレスを返す */
static uint64_t alloc_table(void)
{
	uint64_t *t;

	if (spare_used >= SPARE_PAGES)
		panic("mmio: out of spare page tables");
	t = spare_tables[spare_used++];
	for (size_t i = 0; i < 512; i++)
		t[i] = 0;
	return kernel_virt_to_phys(t);
}

/*
 * 大きいページ（level 3 なら 1GiB、level 2 なら 2MiB）を、一段小さいページ 512 個に分割する。
 * 割り当て先と属性は元の大きいページのまま引き継ぐので、見え方は変わらない。
 * これで、その範囲の一部（MMIO の 4KiB）だけ属性を変えられるようになる。
 */
static void split_big_page(uint64_t *entry, int level)
{
	uint64_t big = *entry;
	uint64_t base = big & PTE_ADDR_MASK & ~((1ULL << (12 + 9 * (level - 1))) - 1);
	uint64_t child_size = 1ULL << (12 + 9 * (level - 2));
	/*
	 * 属性ビット。大きいページの PAT ビット（bit 12）はアドレス欄と重なる位置にあるので、
	 * ~PTE_ADDR_MASK だけでは取りこぼす。別に取り出しておく。
	 */
	uint64_t flags = big & ~PTE_ADDR_MASK; /* NX（bit 63）、PS、PCD/PWT なども含む */
	bool pat = (big & PTE_PAT_BIG) != 0;
	uint64_t table_phys = alloc_table();
	uint64_t *table = phys_to_virt(table_phys);

	if (level == 2) {
		/* 2MiB → 4KiB: PS を外し、PAT ビットを bit 7（4KiB ページでの位置）に置く */
		flags &= ~PTE_PS;
		if (pat)
			flags |= PTE_PAT_4K;
	} else if (pat) {
		/* 1GiB → 2MiB: 子も大きいページなので、PS はそのまま、PAT も bit 12 のまま */
		flags |= PTE_PAT_BIG;
	}
	for (uint64_t i = 0; i < 512; i++)
		table[i] = (base + i * child_size) | flags;

	/*
	 * 途中の段の項目として差し替える。途中の段の権限は「子の権限との AND」で効くので、
	 * 最も緩い値（書き込み可・実行可）にしておき、実際の権限は子の項目で決める。
	 * 元の大きいページの属性（NX など）は子の各項目へ引き継いだので失われない。
	 */
	*entry = table_phys | PTE_P | PTE_RW;
}

/* 1 ページ（4KiB）を割り当てる。mmio_lock を持った状態で呼ぶ */
static void map_page(uint64_t virt, uint64_t phys, bool nx_supported)
{
	uint64_t *table = phys_to_virt(cpu_read_cr3() & PTE_ADDR_MASK);
	int levels = (read_cr4() & CR4_LA57) ? 5 : 4;
	uint64_t leaf;

	for (int level = levels; level > 1; level--) {
		uint64_t index = (virt >> (12 + 9 * (level - 1))) & 0x1ff;
		uint64_t *entry = &table[index];

		if (!(*entry & PTE_P)) {
			*entry = alloc_table() | PTE_P | PTE_RW;
		} else if (*entry & PTE_PS) {
			/* 大きいページで終わっている段。4KiB の段まで分割してから進む */
			split_big_page(entry, level);
		} else if (!(*entry & PTE_RW)) {
			/*
			 * 途中の段が書き込み禁止だと、末端で書き込み可にしても書けない（権限は AND）。
			 * HHDM の途中の段を緩めても、他のページの権限は各自の末端の項目で決まるので
			 * 影響はない。NX も同様に途中の段で立っていると実行できないが、MMIO は実行しない
			 * ので問題にならない。
			 */
			*entry |= PTE_RW;
		}
		table = table_virt(*entry);
	}

	leaf = phys | PTE_P | PTE_RW | PTE_PCD | PTE_PWT;
	if (nx_supported)
		leaf |= PTE_NX;

	{
		uint64_t *entry = &table[(virt >> 12) & 0x1ff];

		/*
		 * すでに別の物理アドレスへ割り当てられていたら、HHDM の約束（virt = phys + hhdm）が
		 * 崩れているので異常。同じ物理アドレスなら、キャッシュ無効の属性で上書きする。
		 */
		if ((*entry & PTE_P) && (*entry & PTE_ADDR_MASK) != phys)
			panic("mmio: virt 0x%llx already maps phys 0x%llx (wanted 0x%llx)",
			      (unsigned long long)virt,
			      (unsigned long long)(*entry & PTE_ADDR_MASK),
			      (unsigned long long)phys);
		*entry = leaf;
	}
	cpu_invlpg(virt);
}

volatile void *mmio_map(uint64_t phys, size_t size)
{
	uint64_t hhdm = boot_info_get()->hhdm_offset;
	uint64_t start = phys & ~(PAGE_SIZE - 1);
	uint64_t end = (phys + (size ? size : 1) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	/* NX ビットは EFER.NXE が有効なときだけ使える（無効だと予約ビット扱いでページフォルト） */
	bool nx_supported = (cpu_rdmsr(MSR_EFER) & EFER_NXE) != 0;
	uint64_t flags = spin_lock_irqsave(&mmio_lock);

	for (uint64_t p = start; p < end; p += PAGE_SIZE)
		map_page(p + hhdm, p, nx_supported);

	spin_unlock_irqrestore(&mmio_lock, flags);
	return (volatile void *)(uintptr_t)(phys + hhdm);
}
