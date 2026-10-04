/*
 * ACPI（MADT）、Local APIC、MMIO の割り当て、タイマー割り込みの確認。
 *
 * ktest は kmain が割り込みを許可し、タイマーが動いていることを確かめた後に実行される。
 * QEMU（q35）の構成を前提にした確認もある（Local APIC の位置、IRQ 0 の上書きなど）。
 */
#include <stdbool.h>
#include <stdint.h>

#include <kui/acpi.h>
#include <kui/arch/x86_64/apic.h>
#include <kui/arch/x86_64/cpu.h>
#include <kui/boot_info.h>
#include <kui/ktest.h>
#include <kui/mmio.h>
#include <kui/timer.h>

KTEST(acpi_madt_found)
{
	const struct acpi_madt_info *madt = acpi_madt();

	KEXPECT(madt != 0);
	if (madt == 0)
		return;
	KEXPECT(madt->cpu_count >= 1);
	KEXPECT(madt->ioapic_count >= 1);
	/* QEMU の Local APIC は標準の位置 0xfee00000 にある */
	KEXPECT_EQ(madt->lapic_phys & ~0xfffULL, 0xfee00000ULL);
	/* QEMU は IRQ 0（PIT）を GSI 2 へ上書きする ISO を用意する */
	KEXPECT_EQ(acpi_isa_irq_to_gsi(madt, 0, 0), 2);
}

KTEST(lapic_id_is_listed_in_madt)
{
	const struct acpi_madt_info *madt = acpi_madt();
	uint32_t id = lapic_id();
	bool found = false;

	KEXPECT(madt != 0);
	if (madt == 0)
		return;
	for (size_t i = 0; i < madt->cpu_count; i++) {
		if (madt->cpu_apic_ids[i] == id)
			found = true;
	}
	KEXPECT(found);
}

KTEST(mmio_map_is_idempotent)
{
	const struct acpi_madt_info *madt = acpi_madt();
	uint64_t phys = madt != 0 ? madt->lapic_phys : 0xfee00000ULL;
	volatile void *a = mmio_map(phys, 0x1000);
	volatile void *b = mmio_map(phys, 0x400);

	/* 同じ物理アドレスは同じ仮想アドレス（phys + hhdm）になる */
	KEXPECT(a == b);
	KEXPECT_EQ((uint64_t)(uintptr_t)a, phys + boot_info_get()->hhdm_offset);
	/* 割り当て後は実際に読める（Local APIC の ID レジスタは xAPIC では上位 8 ビットが ID） */
	if (!(cpu_rdmsr(0x1b) & (1ULL << 10)))
		KEXPECT_EQ(mmio_read32(a, 0x20) >> 24, lapic_id());
}

KTEST(timer_ticks_advance)
{
	uint64_t t0 = timer_ticks();

	KEXPECT(cpu_interrupts_enabled());
	KEXPECT_EQ(timer_hz(), 100);
	timer_wait_ticks(5);
	KEXPECT(timer_ticks() >= t0 + 5);
}

/*
 * tick の間隔がおおむね正しいか。PIT で TSC の速さを測り、10 tick（100Hz なら 100ms）の
 * 間に進んだ TSC と比べる。KVM なし（TCG）や CI の遅い環境でも落ちないよう、
 * 許容幅は 0.5〜2 倍と緩くしている。
 */
KTEST(timer_interval_is_roughly_correct)
{
	uint64_t tsc0, tsc_per_10ms, t, start, elapsed, expected;

	tsc0 = cpu_rdtsc();
	pit_wait_us(10000);
	tsc_per_10ms = cpu_rdtsc() - tsc0;
	KEXPECT(tsc_per_10ms > 0);

	/* tick の境目から測り始める */
	t = timer_ticks();
	while (timer_ticks() == t)
		cpu_halt();
	start = cpu_rdtsc();
	timer_wait_ticks(10);
	elapsed = cpu_rdtsc() - start;

	/* 10 tick = 1000 / hz * 10 ms */
	expected = tsc_per_10ms * (1000 / timer_hz());
	KEXPECT(elapsed > expected / 2);
	KEXPECT(elapsed < expected * 2);
}
