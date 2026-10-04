/*
 * 割り込みコントローラ（x86_64）: 8259 PIC、Local APIC、I/O APIC と、
 * Local APIC タイマーの較正に使う PIT（8254）。
 *
 *   - 8259 PIC: 古い割り込みコントローラ。使わないので全 IRQ をマスクし、
 *     念のため例外と重ならないベクタ（0x20〜）へずらしておく。
 *   - Local APIC: CPU ごとの割り込みコントローラ。タイマーを内蔵する。
 *   - I/O APIC: デバイス（キーボードなど）の割り込みを CPU へ配る。
 */
#ifndef KUI_ARCH_X86_64_APIC_H
#define KUI_ARCH_X86_64_APIC_H

#include <stdbool.h>
#include <stdint.h>

/* 8259 PIC を 0x20〜0x2f にずらしたうえで全 IRQ をマスクする */
void pic_disable(void);

/*
 * Local APIC を有効化する（MMIO を mmio_map() で割り当て、スプリアスベクタを 0xff に、
 * タスク優先度を 0 に）。表示: "lapic: id <id>, base 0x<phys>"
 */
void lapic_init(uint64_t lapic_phys);
uint32_t lapic_id(void);

/* 割り込み処理の終わりに呼び、Local APIC に処理完了（End Of Interrupt）を伝える。lapic_init() 前は何もしない */
void lapic_eoi(void);

/* Local APIC タイマー（周期モード）を設定する。initial_count はタイマーの初期値 */
void lapic_timer_start_periodic(uint8_t vector, uint32_t initial_count, uint8_t divide_shift);
void lapic_timer_stop(void);
/* タイマーの現在値（カウントダウン中の値） */
uint32_t lapic_timer_current(void);
/* ワンショットで最大値から数え始める（較正用） */
void lapic_timer_start_oneshot_max(uint8_t divide_shift);

/*
 * MADT の情報から I/O APIC を初期化し、全入力をマスクする。
 * 表示: "ioapic: id <id>, base 0x<phys>, gsi <base>-<last>"（I/O APIC ごと）
 */
void ioapic_init(void);

/*
 * ISA IRQ を、ベクタ vector で今の CPU（Local APIC）へ届くよう設定する。
 * ISO（極性・トリガモードの上書き）を反映する。masked なら設定だけしてマスクのまま。
 */
bool ioapic_route_isa_irq(uint8_t isa_irq, uint8_t vector, bool masked);
void ioapic_set_masked(uint8_t isa_irq, bool masked);

/*
 * PIT のチャンネル 2 を使って、およそ us マイクロ秒だけ待つ（割り込み不要のポーリング）。
 * Local APIC タイマーの較正に使う。最大 50000us。
 */
void pit_wait_us(uint32_t us);

#endif
