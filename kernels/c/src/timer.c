/*
 * タイマー割り込み（仕様は kui/timer.h を参照）。
 *
 * 較正の手順:
 *   1. Local APIC タイマーを 16 分周・ワンショットで最大値から数え下げさせる
 *   2. PIT で 10ms 待つ
 *   3. その間に減った数 ÷ 10 = 1ms あたりのカウント数
 *   4. 1〜3 を 3 回繰り返し、中央値を使う
 * 周期モードの初期値を「1ms あたりのカウント数 × 1000 ÷ hz」にすれば、
 * 1 秒に hz 回の割り込みになる。
 */
#include <stdint.h>

#include <kui/arch/x86_64/apic.h>
#include <kui/arch/x86_64/cpu.h>
#include <kui/arch/x86_64/interrupt.h>
#include <kui/panic.h>
#include <kui/printk.h>
#include <kui/timer.h>

#define TIMER_DIVIDE_SHIFT 4 /* 16 分周 */
#define CALIBRATE_MS       10
#define CALIBRATE_ROUNDS   3 /* 奇数（中央値を取るため） */

/*
 * 割り込み処理だけが書き、他は読むだけ。__atomic で読み書きすれば、
 * 64 ビットの値が途中まで書き換わった状態を読むことはない（x86_64 では
 * 境界のそろった 64 ビットの読み書きはもともと分割されないが、意図を明示する）。
 */
static uint64_t ticks;
static uint32_t hz_configured;

static void timer_interrupt(struct interrupt_frame *frame)
{
	(void)frame;
	__atomic_fetch_add(&ticks, 1, __ATOMIC_RELAXED);
	lapic_eoi();
}

/* Local APIC タイマーが CALIBRATE_MS ミリ秒の間に何カウント進むかを、PIT で測る */
static uint32_t measure_counts(void)
{
	uint32_t elapsed;

	lapic_timer_start_oneshot_max(TIMER_DIVIDE_SHIFT);
	pit_wait_us(CALIBRATE_MS * 1000);
	elapsed = 0xffffffffu - lapic_timer_current();
	lapic_timer_stop();
	return elapsed;
}

/* 小さな配列の挿入ソート（較正の中央値用） */
static void sort_u32(uint32_t *a, int n)
{
	for (int i = 1; i < n; i++) {
		uint32_t v = a[i];
		int j = i - 1;

		while (j >= 0 && a[j] > v) {
			a[j + 1] = a[j];
			j--;
		}
		a[j + 1] = v;
	}
}

void timer_init(uint32_t hz)
{
	uint32_t samples[CALIBRATE_ROUNDS];
	uint32_t elapsed, counts_per_ms;
	uint64_t initial;

	if (hz == 0 || hz > 10000)
		panic("timer: unsupported frequency %u Hz", hz);

	/*
	 * 1 回の測定は、エミュレータ（KVM なしの QEMU など）ではホストの負荷で大きく揺れる。
	 * 何回か測って中央値を使い、たまたま遅れた（または速かった）1 回に引きずられないようにする。
	 */
	for (int i = 0; i < CALIBRATE_ROUNDS; i++)
		samples[i] = measure_counts();
	sort_u32(samples, CALIBRATE_ROUNDS);
	elapsed = samples[CALIBRATE_ROUNDS / 2];

	counts_per_ms = elapsed / CALIBRATE_MS;
	if (counts_per_ms == 0)
		panic("timer: lapic timer calibration failed (elapsed %u)", elapsed);

	initial = (uint64_t)counts_per_ms * 1000 / hz;
	if (initial == 0 || initial > 0xffffffffu)
		panic("timer: initial count out of range (%llu)", (unsigned long long)initial);

	hz_configured = hz;
	interrupt_register(VEC_LAPIC_TIMER, timer_interrupt);
	lapic_timer_start_periodic(VEC_LAPIC_TIMER, (uint32_t)initial, TIMER_DIVIDE_SHIFT);
	printk("timer: lapic %u counts/ms, %u Hz\n", counts_per_ms, hz);
}

uint64_t timer_ticks(void)
{
	return __atomic_load_n(&ticks, __ATOMIC_RELAXED);
}

uint32_t timer_hz(void)
{
	return hz_configured;
}

void timer_wait_ticks(uint64_t n)
{
	uint64_t target = timer_ticks() + n;

	/* 割り込みが禁止されていると、hlt から永久に戻らない */
	if (!cpu_interrupts_enabled())
		panic("timer_wait_ticks called with interrupts disabled");
	while (timer_ticks() < target)
		cpu_halt();
}
