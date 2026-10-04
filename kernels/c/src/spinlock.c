/*
 * スピンロックの実装（仕様は kui/spinlock.h を参照）。
 *
 * 取得: locked を不可分（atomic）に 1 と交換し、元の値が 0 なら取得成功。
 *       元が 1 なら誰かが使用中なので、空くまで pause しながら読み続ける。
 * 解放: locked に 0 を書く。
 *
 * メモリの順序: 取得には ACQUIRE、解放には RELEASE を付ける。これで、ロック中に
 * 行った読み書きがロックの外へはみ出して見えることを、コンパイラにも CPU にも防がせる。
 *
 * デッドロックの検出（単一 CPU の前提）:
 *   今は CPU が 1 つだけなので、ロックが使用中のまま割り込み禁止の状態で待ち始めたら、
 *   ロックを放せる処理（同じ CPU 上の、割り込まれた側の処理）は二度と動かない。
 *   つまり確実にデッドロックなので、待たずに panic する。
 *   マルチ CPU に対応するとき（M6 以降）は、「他の CPU が持っている」場合を区別するため、
 *   持ち主の CPU 番号を記録して比較するように変える必要がある。
 */
#include <stdbool.h>
#include <stdint.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/panic.h>
#include <kui/printk.h>
#include <kui/spinlock.h>

/* この回数だけ待っても取れなければ警告を出す（デバッグ用。取得はそのまま待ち続ける） */
#define SPIN_WARN_ITERATIONS (1u << 26)

void spin_lock_init(struct spinlock *lock, const char *name)
{
	lock->locked = 0;
	lock->name = name;
	lock->owner_pc = 0;
}

static bool try_acquire(struct spinlock *lock)
{
	return __atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE) == 0;
}

static _Noreturn void report_deadlock(struct spinlock *lock, void *pc)
{
	panic("spinlock: deadlock: '%s' is already held (acquired at %p, requested at %p)",
	      lock->name ? lock->name : "?", lock->owner_pc, pc);
}

/* ロックを取る本体。pc は取得を依頼した呼び出し元（デバッグ表示用） */
static void acquire(struct spinlock *lock, void *pc)
{
	uint32_t spins = 0;
	bool warned = false;

	while (!try_acquire(lock)) {
		/* 割り込み禁止中に使用中のロックを待つ = 単一 CPU では必ずデッドロック */
		if (!cpu_interrupts_enabled())
			report_deadlock(lock, pc);

		/* 空くまでは書き込まずに読むだけで待つ（不要なバス操作を減らす） */
		while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) {
			cpu_pause();
			if (!warned && ++spins >= SPIN_WARN_ITERATIONS) {
				/* printk はこのロック自身かもしれないので、ロックを使わない経路で出す */
				printk_emergency("spinlock: warning: waiting long for '%s' "
						 "(held at %p, requested at %p)\n",
						 lock->name ? lock->name : "?", lock->owner_pc, pc);
				warned = true;
			}
		}
	}
	lock->owner_pc = pc;
}

static void release(struct spinlock *lock)
{
	if (!__atomic_load_n(&lock->locked, __ATOMIC_RELAXED))
		panic("spinlock: unlocking '%s' which is not held", lock->name ? lock->name : "?");
	lock->owner_pc = 0;
	__atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

void spin_lock(struct spinlock *lock)
{
	acquire(lock, __builtin_return_address(0));
}

void spin_unlock(struct spinlock *lock)
{
	release(lock);
}

uint64_t spin_lock_irqsave(struct spinlock *lock)
{
	/* 先に割り込みを禁止する。逆順だと、取得直後に割り込まれてデッドロックし得る */
	uint64_t flags = cpu_irq_save();

	acquire(lock, __builtin_return_address(0));
	return flags;
}

void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags)
{
	release(lock);
	cpu_irq_restore(flags);
}

bool spin_trylock(struct spinlock *lock)
{
	if (!try_acquire(lock))
		return false;
	lock->owner_pc = __builtin_return_address(0);
	return true;
}

bool spin_is_locked(const struct spinlock *lock)
{
	return __atomic_load_n(&lock->locked, __ATOMIC_RELAXED) != 0;
}
