/*
 * I/O APIC（デバイスの割り込みを CPU へ配るコントローラ）。
 *
 * レジスタは 2 つの窓口だけで読み書きする:
 *   IOREGSEL（オフセット 0x00）にレジスタ番号を書き、
 *   IOWIN   （オフセット 0x10）でそのレジスタを読み書きする。
 * 「番号を書いてから読み書き」の 2 手順の間に割り込まれると壊れるので、ロックで守る。
 *
 * 入力ピンごとに「リダイレクション表」の項目（64 ビット）があり、
 * どのベクタで、どの CPU へ、どの極性・トリガモードで届けるかを設定する。
 * 入力ピンの番号に gsi_base を足したものが GSI（Global System Interrupt）番号。
 */
#include <stdbool.h>
#include <stdint.h>

#include <kui/acpi.h>
#include <kui/arch/x86_64/apic.h>
#include <kui/mmio.h>
#include <kui/printk.h>
#include <kui/spinlock.h>

#define IOREGSEL 0x00
#define IOWIN    0x10

#define IOAPIC_REG_ID     0x00
#define IOAPIC_REG_VER    0x01
#define IOAPIC_REG_REDTBL 0x10 /* 項目 n の下位 32 ビットは 0x10 + 2n、上位は 0x11 + 2n */

#define REDTBL_ACTIVE_LOW (1u << 13)
#define REDTBL_LEVEL      (1u << 15)
#define REDTBL_MASKED     (1u << 16)

/* ACPI の MPS INTI flags */
#define INTI_POLARITY_MASK   0x3
#define INTI_POLARITY_LOW    0x3
#define INTI_TRIGGER_MASK    0xc
#define INTI_TRIGGER_LEVEL   0xc

struct ioapic {
	volatile void *mmio;
	uint32_t gsi_base;
	uint32_t gsi_count;
	uint8_t id;
};

static struct ioapic ioapics[ACPI_MAX_IOAPICS];
static size_t ioapic_count;
static struct spinlock ioapic_lock = SPINLOCK_INIT("ioapic");

static uint32_t ioapic_read(const struct ioapic *io, uint32_t reg)
{
	mmio_write32(io->mmio, IOREGSEL, reg);
	return mmio_read32(io->mmio, IOWIN);
}

static void ioapic_write(const struct ioapic *io, uint32_t reg, uint32_t value)
{
	mmio_write32(io->mmio, IOREGSEL, reg);
	mmio_write32(io->mmio, IOWIN, value);
}

void ioapic_init(void)
{
	const struct acpi_madt_info *madt = acpi_madt();
	uint64_t flags;

	if (madt == NULL) {
		printk("ioapic: no MADT, skipping\n");
		return;
	}

	flags = spin_lock_irqsave(&ioapic_lock);
	ioapic_count = 0;
	for (size_t i = 0; i < madt->ioapic_count; i++) {
		struct ioapic *io = &ioapics[ioapic_count++];

		io->mmio = mmio_map(madt->ioapics[i].phys, 0x20);
		io->id = madt->ioapics[i].id;
		io->gsi_base = madt->ioapics[i].gsi_base;
		/* バージョンレジスタの bit 16〜23 は「最後の項目の番号」（数 - 1） */
		io->gsi_count = ((ioapic_read(io, IOAPIC_REG_VER) >> 16) & 0xff) + 1;

		/* 全入力をマスクする。勝手に割り込みが来ないよう、使う入力だけ後で開ける */
		for (uint32_t pin = 0; pin < io->gsi_count; pin++) {
			ioapic_write(io, IOAPIC_REG_REDTBL + 2 * pin, REDTBL_MASKED);
			ioapic_write(io, IOAPIC_REG_REDTBL + 2 * pin + 1, 0);
		}
	}
	spin_unlock_irqrestore(&ioapic_lock, flags);

	for (size_t i = 0; i < ioapic_count; i++)
		printk("ioapic: id %u, base 0x%llx, gsi %u-%u\n", (unsigned)ioapics[i].id,
		       (unsigned long long)madt->ioapics[i].phys, ioapics[i].gsi_base,
		       ioapics[i].gsi_base + ioapics[i].gsi_count - 1);
}

/* GSI を担当する I/O APIC を探し、入力ピンの番号を *pin に入れる */
static struct ioapic *find_ioapic(uint32_t gsi, uint32_t *pin)
{
	for (size_t i = 0; i < ioapic_count; i++) {
		struct ioapic *io = &ioapics[i];

		if (gsi >= io->gsi_base && gsi - io->gsi_base < io->gsi_count) {
			*pin = gsi - io->gsi_base;
			return io;
		}
	}
	return NULL;
}

bool ioapic_route_isa_irq(uint8_t isa_irq, uint8_t vector, bool masked)
{
	uint16_t inti = 0;
	uint32_t gsi = acpi_isa_irq_to_gsi(acpi_madt(), isa_irq, &inti);
	uint32_t pin, dest, low = vector;
	struct ioapic *io;
	uint64_t flags;

	io = find_ioapic(gsi, &pin);
	if (io == NULL) {
		printk("ioapic: no ioapic handles gsi %u (isa irq %u)\n", gsi, (unsigned)isa_irq);
		return false;
	}

	/*
	 * 極性とトリガモード。ISO で指定がなければ（00 = バスの既定）、ISA の既定である
	 * 「active high・edge」にする。
	 */
	if ((inti & INTI_POLARITY_MASK) == INTI_POLARITY_LOW)
		low |= REDTBL_ACTIVE_LOW;
	if ((inti & INTI_TRIGGER_MASK) == INTI_TRIGGER_LEVEL)
		low |= REDTBL_LEVEL;
	if (masked)
		low |= REDTBL_MASKED;
	/* 配送モード Fixed（000）、宛先は物理モードで今の CPU */

	/*
	 * 宛先は物理宛先モードの 8 ビット APIC ID。x2APIC の 255 を超える ID には届けられない
	 * （割り込みの再マッピングが必要になる）。
	 */
	dest = lapic_id();
	if (dest > 0xff) {
		printk("ioapic: apic id %u does not fit the 8-bit destination\n", dest);
		return false;
	}

	flags = spin_lock_irqsave(&ioapic_lock);
	/*
	 * 項目は 32 ビットずつ 2 回に分けて書くので、書き換えの途中の「半端な設定」で
	 * 割り込みが届かないよう、まずマスクしてから上位（宛先）、最後に下位
	 * （ベクタ・極性・トリガ・マスク解除）を書く。
	 */
	ioapic_write(io, IOAPIC_REG_REDTBL + 2 * pin, REDTBL_MASKED);
	ioapic_write(io, IOAPIC_REG_REDTBL + 2 * pin + 1, dest << 24);
	ioapic_write(io, IOAPIC_REG_REDTBL + 2 * pin, low);
	spin_unlock_irqrestore(&ioapic_lock, flags);
	return true;
}

void ioapic_set_masked(uint8_t isa_irq, bool masked)
{
	uint32_t gsi = acpi_isa_irq_to_gsi(acpi_madt(), isa_irq, NULL);
	uint32_t pin, low;
	struct ioapic *io = find_ioapic(gsi, &pin);
	uint64_t flags;

	if (io == NULL)
		return;
	flags = spin_lock_irqsave(&ioapic_lock);
	low = ioapic_read(io, IOAPIC_REG_REDTBL + 2 * pin);
	if (masked)
		low |= REDTBL_MASKED;
	else
		low &= ~REDTBL_MASKED;
	ioapic_write(io, IOAPIC_REG_REDTBL + 2 * pin, low);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}
