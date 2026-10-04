/*
 * スピンロック。
 *
 * 共有データを複数の実行の流れ（いまは「通常の処理」と「割り込み処理」、
 * 将来は複数 CPU）から守る。ロックが空くまでループで待つ。
 *
 * 割り込み処理の中でも取るロックは、必ず spin_lock_irqsave() で取ること。
 * 通常の処理がロックを持ったまま割り込まれ、割り込み処理が同じロックを待つと、
 * 永久に待ち続ける（デッドロック）。割り込みを禁止してから取れば、これが起きない。
 *
 * デバッグ: 同じ CPU が同じロックを二重に取ろうとしたら panic する。
 */
#ifndef KUI_SPINLOCK_H
#define KUI_SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

struct spinlock {
	volatile uint32_t locked; /* 0: 空き、1: 使用中 */
	const char *name;         /* デバッグ表示用 */
	void *owner_pc;           /* ロックを取った場所（デバッグ用） */
};

#define SPINLOCK_INIT(lock_name) { .locked = 0, .name = (lock_name), .owner_pc = 0 }

void spin_lock_init(struct spinlock *lock, const char *name);

/* 割り込みの状態を変えずに取る／放す（割り込み処理から取られないロック用） */
void spin_lock(struct spinlock *lock);
void spin_unlock(struct spinlock *lock);

/* 割り込みを禁止してから取る。戻り値を spin_unlock_irqrestore() に渡す */
uint64_t spin_lock_irqsave(struct spinlock *lock);
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags);

/* 空いていれば取って true、使用中なら何もせず false */
bool spin_trylock(struct spinlock *lock);

bool spin_is_locked(const struct spinlock *lock);

#endif
