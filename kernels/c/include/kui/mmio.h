/*
 * MMIO（メモリ上に割り当てられたデバイスのレジスタ）へのアクセス。
 *
 * Local APIC（0xfee00000）や I/O APIC（0xfec00000）のレジスタは物理アドレス上にあるが、
 * Limine の直接マップ（HHDM）はメモリマップに載った領域しか割り当てないため、
 * そのままでは読み書きできない。mmio_map() で HHDM 上の同じ位置（phys + hhdm_offset）に
 * キャッシュ無効で割り当ててから使う。
 *
 * M1 では Limine の作ったページテーブルに直接書き足す。途中の段のページテーブルが
 * 足りなければ、カーネル内の静的な予備ページ（数ページ）から補う。
 * M2 で自前のページテーブルに切り替えたら、そちらの仕組みに置き換える。
 */
#ifndef KUI_MMIO_H
#define KUI_MMIO_H

#include <stddef.h>
#include <stdint.h>

/*
 * 物理アドレス phys から size バイトを、キャッシュ無効（PCD|PWT）・書き込み可・実行不可で
 * 割り当て、その仮想アドレスを返す。すでに割り当て済みのページはそのまま使う。
 * 予備ページが尽きたら panic する。
 */
volatile void *mmio_map(uint64_t phys, size_t size);

static inline uint32_t mmio_read32(volatile void *base, uint32_t offset)
{
	return *(volatile uint32_t *)((volatile uint8_t *)base + offset);
}

static inline void mmio_write32(volatile void *base, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)((volatile uint8_t *)base + offset) = value;
}

/* 物理アドレスを HHDM 上の仮想アドレスに変換する（メモリマップに載った領域のみ有効） */
void *phys_to_virt(uint64_t phys);

#endif
