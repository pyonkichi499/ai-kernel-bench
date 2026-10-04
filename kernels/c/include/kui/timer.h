/*
 * 時間の管理（タイマー割り込み）。
 *
 * Local APIC タイマーを周期モードで動かし、一定間隔（hz 回/秒）で割り込みを起こす。
 * Local APIC タイマーの速さは機種ごとに違うので、起動時に PIT（周波数が既知）で
 * 一定時間を測り、その間に何カウント進むかで較正する。
 */
#ifndef KUI_TIMER_H
#define KUI_TIMER_H

#include <stdint.h>

/*
 * タイマーを較正して hz 回/秒で割り込みを起こすよう設定する（割り込みはまだ許可しない）。
 * 表示: "timer: lapic <n> counts/ms, <hz> Hz"
 */
void timer_init(uint32_t hz);

/* 起動してからのタイマー割り込みの回数 */
uint64_t timer_ticks(void);
uint32_t timer_hz(void);

/* ticks 回分のタイマー割り込みが来るまで hlt で待つ（割り込み許可状態で呼ぶこと） */
void timer_wait_ticks(uint64_t ticks);

#endif
