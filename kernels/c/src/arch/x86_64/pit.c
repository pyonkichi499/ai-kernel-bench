/*
 * PIT（8254 Programmable Interval Timer）による短時間の待ち。
 *
 * PIT は 1.193182MHz の固定周波数で数を数えるタイマーで、周波数が分かっているので
 * 「本当の時間」の物差しとして使える。Local APIC タイマーは機種ごとに速さが違うため、
 * 起動時に PIT で一定時間を測って較正する。
 *
 * チャンネル 2（本来は PC スピーカー用）は、ゲート（ポート 0x61 の bit 0）で数え始めを
 * 制御でき、数え終わったかどうか（出力）をポート 0x61 の bit 5 で読めるので、
 * 割り込みを使わずにポーリングで待てる。
 */
#include <stdint.h>

#include <kui/arch/x86_64/apic.h>
#include <kui/arch/x86_64/cpu.h>
#include <kui/arch/x86_64/io.h>
#include <kui/panic.h>

#define PIT_FREQ_HZ  1193182u
#define PIT_CH2_DATA 0x42
#define PIT_CMD      0x43
#define PORT_B       0x61 /* キーボードコントローラのポート B（PIT チャンネル 2 の制御） */

#define PORTB_GATE2   0x01 /* チャンネル 2 のゲート（1 で数える） */
#define PORTB_SPEAKER 0x02 /* スピーカーへの出力（鳴らさないので 0） */
#define PORTB_OUT2    0x20 /* チャンネル 2 の出力（読み取り専用） */

/* コマンド: チャンネル 2、下位→上位の順に書く、モード 0（数え終わると出力が 1 になる）、2 進数 */
#define PIT_CMD_CH2_MODE0 0xb0

#define PIT_MAX_US 50000u

void pit_wait_us(uint32_t us)
{
	uint32_t count;
	uint8_t portb;

	if (us == 0)
		return;
	if (us > PIT_MAX_US)
		us = PIT_MAX_US;
	/* 50000us でも 59659 なので 16 ビットに収まる */
	count = (uint32_t)(((uint64_t)PIT_FREQ_HZ * us + 999999) / 1000000);

	/* ゲートを閉じ、スピーカーを切ってからカウンタを設定する */
	portb = inb(PORT_B) & (uint8_t)~(PORTB_GATE2 | PORTB_SPEAKER);
	outb(PORT_B, portb);
	outb(PIT_CMD, PIT_CMD_CH2_MODE0);
	outb(PIT_CH2_DATA, (uint8_t)count);
	outb(PIT_CH2_DATA, (uint8_t)(count >> 8));

	/* ゲートを開けて数え始め、出力が 1 になる（数え終わる）まで待つ */
	outb(PORT_B, portb | PORTB_GATE2);
	for (uint64_t spins = 0; !(inb(PORT_B) & PORTB_OUT2); spins++) {
		/*
		 * PIT がない環境で永久に待たないための安全装置。inb 1 回は少なくとも
		 * 数百ナノ秒かかるので、この回数なら最大待ち時間（50ms）より十分長い。
		 */
		if (spins > 100000000ULL)
			panic("pit: channel 2 did not count down");
		cpu_pause();
	}

	outb(PORT_B, portb);
}
