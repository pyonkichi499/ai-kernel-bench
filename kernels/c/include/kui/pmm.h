/*
 * 物理メモリ管理（Physical Memory Manager）。
 *
 * 物理メモリを 4KiB の「ページ」単位で管理する。方式はバディアロケータ:
 * 2^order 個の連続したページを 1 つの塊として扱い、order ごとの空きリストを持つ。
 * 塊を解放するとき、隣の「相方（バディ）」も空いていれば結合して大きな塊に戻す。
 * 連続した物理ページ（DMA 用バッファなど）を確保でき、断片化も抑えられる。
 *
 * 各ページには管理情報 struct page（参照カウントなど）を 1 つずつ持つ。
 * 参照カウントは、将来 fork のコピーオンライト（M6）でページを共有するときに使う。
 *
 * 除外する領域:
 *   - 物理アドレス 0〜1MiB: 0 番地を NULL と区別するため、また将来 AP 起動用のコードを置くため
 *   - usable 以外の領域（ブートローダー再利用可能な領域は pmm_reclaim_bootloader() で後から加える）
 */
#ifndef KUI_PMM_H
#define KUI_PMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/boot_info.h>

#define PAGE_SIZE      4096ULL
#define PAGE_SHIFT     12
#define PMM_MAX_ORDER  10 /* 最大の塊は 2^10 ページ = 4MiB */
#define PMM_LOW_LIMIT  0x100000ULL

#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

struct page {
	uint32_t refcount; /* 0 なら空き（または管理外） */
	uint8_t order;     /* 空きの塊の先頭なら、その塊の order */
	uint8_t flags;     /* PAGE_FLAG_* */
	uint16_t reserved;
	struct page *next; /* 空きリストのつながり */
	struct page *prev;
};

#define PAGE_FLAG_FREE_HEAD 0x01 /* 空きの塊の先頭 */
#define PAGE_FLAG_MANAGED   0x02 /* pmm が管理しているページ */

/*
 * boot_info のメモリマップから usable な領域を登録する。
 * struct page の配列は usable 領域の一部を使って確保する。
 * 表示: "pmm: <total> MiB managed, <free> MiB free"
 */
void pmm_init(const struct boot_info *info);

/* 2^order ページの連続領域を確保し、先頭の物理アドレスを返す（中身は不定）。失敗なら 0 */
uint64_t pmm_alloc_pages(unsigned order);
/* 1 ページ確保して 0 で埋める。失敗なら 0 */
uint64_t pmm_alloc_zeroed_page(void);
void pmm_free_pages(uint64_t phys, unsigned order);

/* 参照カウントの操作（確保直後は 1）。pmm_page_put() が 0 にしたら解放される */
void pmm_page_get(uint64_t phys);
void pmm_page_put(uint64_t phys);
uint32_t pmm_page_refcount(uint64_t phys);

size_t pmm_free_page_count(void);
size_t pmm_total_page_count(void);

/*
 * ブートローダーが使っていた領域（MEM_BOOTLOADER_RECLAIMABLE）を空きに加える。
 * Limine のスタックやページテーブルもここにあるので、自前のスタックとページテーブルへ
 * 切り替えた後でしか呼んではいけない。
 */
void pmm_reclaim_bootloader(const struct boot_info *info);

#endif
