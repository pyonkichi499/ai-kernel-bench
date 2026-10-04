/*
 * カーネルヒープ（kmalloc / kfree）。
 *
 * 小さな確保（〜2048 バイト）は大きさの区分（16, 32, 64, ..., 2048 バイト）ごとに
 * スラブ（1 ページを同じ大きさの区画に分けたもの）から切り出す。
 * 大きな確保は pmm から連続ページを直接もらう。
 * どちらも直接マップ（HHDM）上のアドレスを返す。
 *
 * デバッグビルドでは、解放した領域を 0x6b で埋め、確保した領域を 0xa5 で埋める
 * （初期化忘れや解放後の使用を見つけやすくする）。二重解放は panic する。
 */
#ifndef KUI_KHEAP_H
#define KUI_KHEAP_H

#include <stddef.h>

/* 表示: "kheap: ready (<n> size classes)" */
void kheap_init(void);

/* size バイト確保する（16 バイト境界）。失敗なら NULL */
void *kmalloc(size_t size);
/* 0 で埋めて確保する。n * size のオーバーフローも検査する */
void *kcalloc(size_t n, size_t size);
void kfree(void *ptr);

/* 統計（テストとデバッグ用） */
size_t kheap_bytes_in_use(void);

#endif
