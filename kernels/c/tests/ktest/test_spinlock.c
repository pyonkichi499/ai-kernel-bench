/*
 * スピンロックと、割り込みの禁止・復元（irqsave / irqrestore）の確認。
 *
 * 二重取得によるデッドロックの検出は panic して止まるので、ここではなく
 * 結合テスト（m1_selftest_spinlock）で確かめる。
 */
#include <stdbool.h>
#include <stdint.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/arch/x86_64/interrupt.h>
#include <kui/ktest.h>
#include <kui/printk.h>
#include <kui/spinlock.h>
#include <kui/string.h>

KTEST(spinlock_lock_and_unlock)
{
	struct spinlock lock;

	spin_lock_init(&lock, "test");
	KEXPECT(!spin_is_locked(&lock));
	spin_lock(&lock);
	KEXPECT(spin_is_locked(&lock));
	KEXPECT(lock.owner_pc != 0);
	spin_unlock(&lock);
	KEXPECT(!spin_is_locked(&lock));
	KEXPECT(lock.owner_pc == 0);
}

KTEST(spinlock_static_initializer)
{
	static struct spinlock lock = SPINLOCK_INIT("static");

	KEXPECT(!spin_is_locked(&lock));
	KEXPECT_STREQ(lock.name, "static");
	spin_lock(&lock);
	KEXPECT(spin_is_locked(&lock));
	spin_unlock(&lock);
}

KTEST(spinlock_trylock)
{
	struct spinlock lock = SPINLOCK_INIT("try");

	KEXPECT(spin_trylock(&lock));
	KEXPECT(spin_is_locked(&lock));
	/* 使用中なら失敗し、状態は変えない */
	KEXPECT(!spin_trylock(&lock));
	KEXPECT(spin_is_locked(&lock));
	spin_unlock(&lock);
	KEXPECT(!spin_is_locked(&lock));
	KEXPECT(spin_trylock(&lock));
	spin_unlock(&lock);
}

KTEST(spinlock_irqsave_disables_and_restores)
{
	struct spinlock lock = SPINLOCK_INIT("irq");
	bool was_enabled = cpu_interrupts_enabled();
	uint64_t flags;

	flags = spin_lock_irqsave(&lock);
	KEXPECT(!cpu_interrupts_enabled());
	KEXPECT(spin_is_locked(&lock));
	spin_unlock_irqrestore(&lock, flags);
	KEXPECT(!spin_is_locked(&lock));
	KEXPECT_EQ(cpu_interrupts_enabled(), was_enabled);
}

KTEST(spinlock_irqrestore_keeps_disabled)
{
	struct spinlock lock = SPINLOCK_INIT("irq-off");
	uint64_t outer = cpu_irq_save();
	uint64_t flags;

	/* 元々禁止されていたなら、restore 後も禁止のまま */
	flags = spin_lock_irqsave(&lock);
	spin_unlock_irqrestore(&lock, flags);
	KEXPECT(!cpu_interrupts_enabled());

	cpu_irq_restore(outer);
}

KTEST(spinlock_nested_irqsave)
{
	struct spinlock a = SPINLOCK_INIT("a");
	struct spinlock b = SPINLOCK_INIT("b");
	bool was_enabled = cpu_interrupts_enabled();
	uint64_t fa, fb;

	fa = spin_lock_irqsave(&a);
	fb = spin_lock_irqsave(&b);
	KEXPECT(!cpu_interrupts_enabled());

	/* 内側を放しても、外側を持っている間は禁止のまま */
	spin_unlock_irqrestore(&b, fb);
	KEXPECT(!cpu_interrupts_enabled());
	KEXPECT(spin_is_locked(&a));

	spin_unlock_irqrestore(&a, fa);
	KEXPECT_EQ(cpu_interrupts_enabled(), was_enabled);
}

KTEST(spinlock_ktests_run_with_interrupts_enabled)
{
	/* M1 から、カーネル内テストは割り込み許可（タイマー動作中）の状態で走る */
	KEXPECT(cpu_interrupts_enabled());
}

/*
 * printk の再入: printk のロックを持った状態（= printk の途中）で割り込み・例外が起き、
 * その処理の中で printk しても、デッドロックの panic にならずに出力して戻ってくること。
 * 例外の報告が「spinlock: deadlock」に化けて失われないことの確認。
 */
static volatile bool reentrant_printk_done;

static void reentrant_printk_handler(struct interrupt_frame *frame)
{
	(void)frame;
	printk("ktest: (reentrant printk from interrupt handler)\n");
	reentrant_printk_done = true;
}

KTEST(printk_reentry_from_interrupt_does_not_deadlock)
{
	uint64_t flags;

	reentrant_printk_done = false;
	interrupt_register(VEC_KTEST, reentrant_printk_handler);

	flags = printk_ktest_hold_lock();
	__asm__ volatile("int $0x81" : : : "memory");
	printk_ktest_release_lock(flags);

	interrupt_register(VEC_KTEST, 0);
	KEXPECT(reentrant_printk_done);
	/* ロックは正しく放されている（次の printk が普通に動く） */
	printk("ktest: (printk after reentry)\n");
}
