/*
 * ACPI テーブルの解析（ハードウェア非依存。仕様は kui/acpi.h を参照）。
 *
 * ACPI のテーブルはファームウェアが作ったバイト列で、構造体のフィールドが
 * 自然な境界にそろっているとは限らない（例: MADT のエントリは 2 バイト境界から始まりうる）。
 * そのため構造体へのキャストはせず、1 バイトずつ組み立てて読む。
 *
 * また、ファームウェアの作ったデータは壊れている可能性がある前提で、
 * 長さを必ず確かめてから読む（範囲外を読まない、長さ 0 のエントリで無限ループしない）。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/acpi.h>

/* すべてのシステム記述テーブル（SDT）に共通のヘッダの大きさ */
#define SDT_HEADER_SIZE 36

/* MADT の構造: SDT ヘッダ、Local APIC のアドレス（4）、flags（4）、その後にエントリが並ぶ */
#define MADT_LAPIC_ADDR_OFFSET 36
#define MADT_FLAGS_OFFSET      40
#define MADT_ENTRIES_OFFSET    44

/* MADT の flags */
#define MADT_FLAG_PCAT_COMPAT (1u << 0) /* 8259 PIC が存在する */

/* MADT のエントリの種類 */
#define MADT_TYPE_LAPIC          0
#define MADT_TYPE_IOAPIC         1
#define MADT_TYPE_ISO            2
#define MADT_TYPE_LAPIC_OVERRIDE 5
#define MADT_TYPE_X2APIC         9

/* Processor Local APIC / x2APIC エントリの flags */
#define LAPIC_FLAG_ENABLED (1u << 0)

static uint16_t read16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint64_t read64(const uint8_t *p)
{
	return (uint64_t)read32(p) | ((uint64_t)read32(p + 4) << 32);
}

bool acpi_checksum_ok(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint8_t sum = 0;

	for (size_t i = 0; i < len; i++)
		sum = (uint8_t)(sum + p[i]);
	return sum == 0;
}

/*
 * 有効な CPU を 1 つ記録する。
 *
 * 仕様では APIC ID が 255 未満の CPU は Processor Local APIC エントリ（型 0）で、
 * それ以上は x2APIC エントリ（型 9）で書くことになっているが、同じ CPU を両方の型で
 * 重ねて書くファームウェアもあるので、同じ ID は 1 回だけ数える。
 * 「起動後に有効化できる（online capable）」だけの CPU は、今は数えない。
 */
static void add_cpu(struct acpi_madt_info *out, uint32_t apic_id)
{
	/* cpu_apic_ids は 8 ビットなので、255 を超える x2APIC ID は記録できない */
	if (apic_id > 0xff)
		return;
	for (size_t i = 0; i < out->cpu_count; i++) {
		if (out->cpu_apic_ids[i] == apic_id)
			return;
	}
	if (out->cpu_count >= ACPI_MAX_CPUS)
		return;
	out->cpu_apic_ids[out->cpu_count++] = (uint8_t)apic_id;
}

static void zero_info(struct acpi_madt_info *out)
{
	uint8_t *p = (uint8_t *)out;

	for (size_t i = 0; i < sizeof(*out); i++)
		p[i] = 0;
}

bool acpi_parse_madt(const void *madt, size_t length, struct acpi_madt_info *out)
{
	const uint8_t *p = madt;
	uint32_t table_len;
	size_t off;

	zero_info(out);
	if (madt == NULL || length < MADT_ENTRIES_OFFSET)
		return false;
	if (p[0] != 'A' || p[1] != 'P' || p[2] != 'I' || p[3] != 'C')
		return false;

	/* ヘッダに書かれた長さが、渡された範囲を超えていたら壊れている */
	table_len = read32(p + 4);
	if (table_len < MADT_ENTRIES_OFFSET || table_len > length)
		return false;

	out->lapic_phys = read32(p + MADT_LAPIC_ADDR_OFFSET);
	out->has_legacy_pic = (read32(p + MADT_FLAGS_OFFSET) & MADT_FLAG_PCAT_COMPAT) != 0;

	off = MADT_ENTRIES_OFFSET;
	while (off < table_len) {
		const uint8_t *e = p + off;
		uint8_t type, elen;

		/* エントリの先頭 2 バイト（種類と長さ）すら読めないなら壊れている */
		if (table_len - off < 2)
			return false;
		type = e[0];
		elen = e[1];
		/* 長さ 2 未満だと先へ進めず無限ループになる。範囲外へはみ出すのも壊れている */
		if (elen < 2 || elen > table_len - off)
			return false;

		switch (type) {
		case MADT_TYPE_LAPIC:
			/* 種類(1) 長さ(1) プロセッサ ID(1) APIC ID(1) flags(4) */
			if (elen < 8)
				return false;
			if (read32(e + 4) & LAPIC_FLAG_ENABLED)
				add_cpu(out, e[3]);
			break;
		case MADT_TYPE_IOAPIC:
			/* 種類(1) 長さ(1) I/O APIC ID(1) 予約(1) アドレス(4) GSI の基点(4) */
			if (elen < 12)
				return false;
			if (out->ioapic_count < ACPI_MAX_IOAPICS) {
				struct acpi_ioapic *io = &out->ioapics[out->ioapic_count++];

				io->id = e[2];
				io->phys = read32(e + 4);
				io->gsi_base = read32(e + 8);
			}
			break;
		case MADT_TYPE_ISO:
			/* 種類(1) 長さ(1) バス(1) 元の IRQ(1) GSI(4) flags(2) */
			if (elen < 10)
				return false;
			if (out->iso_count < ACPI_MAX_ISOS) {
				struct acpi_iso *iso = &out->isos[out->iso_count++];

				iso->isa_irq = e[3];
				iso->gsi = read32(e + 4);
				iso->flags = read16(e + 8);
			}
			break;
		case MADT_TYPE_LAPIC_OVERRIDE:
			/* 種類(1) 長さ(1) 予約(2) 64bit の Local APIC アドレス(8) */
			if (elen < 12)
				return false;
			out->lapic_phys = read64(e + 4);
			break;
		case MADT_TYPE_X2APIC:
			/* 種類(1) 長さ(1) 予約(2) x2APIC ID(4) flags(4) ACPI UID(4) */
			if (elen < 16)
				return false;
			if (read32(e + 8) & LAPIC_FLAG_ENABLED)
				add_cpu(out, read32(e + 4));
			break;
		default:
			/* M1 で使わない種類は読み飛ばす */
			break;
		}
		off += elen;
	}
	return true;
}

uint32_t acpi_isa_irq_to_gsi(const struct acpi_madt_info *info, uint8_t isa_irq,
			     uint16_t *flags_out)
{
	if (info != NULL) {
		for (size_t i = 0; i < info->iso_count; i++) {
			if (info->isos[i].isa_irq == isa_irq) {
				if (flags_out != NULL)
					*flags_out = info->isos[i].flags;
				return info->isos[i].gsi;
			}
		}
	}
	/* 上書きがなければ、ISA の IRQ 番号がそのまま GSI になる */
	if (flags_out != NULL)
		*flags_out = 0;
	return isa_irq;
}
