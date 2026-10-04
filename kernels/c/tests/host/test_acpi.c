/*
 * src/lib/acpi_tables.c のテスト。
 *
 * 作り物の MADT をバイト列として組み立て、正常系と、壊れた入力
 * （短すぎる長さ、長さ 0 のエントリ、範囲外へはみ出すエントリ、上限超過）を確かめる。
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kui/acpi.h>

#include "test.h"

/* テスト用の MADT を組み立てるための小さな道具 */
struct madt_builder {
	uint8_t buf[1024];
	size_t len;
};

static void put8(struct madt_builder *b, uint8_t v)
{
	b->buf[b->len++] = v;
}

static void put16(struct madt_builder *b, uint16_t v)
{
	put8(b, (uint8_t)v);
	put8(b, (uint8_t)(v >> 8));
}

static void put32(struct madt_builder *b, uint32_t v)
{
	put16(b, (uint16_t)v);
	put16(b, (uint16_t)(v >> 16));
}

static void put64(struct madt_builder *b, uint64_t v)
{
	put32(b, (uint32_t)v);
	put32(b, (uint32_t)(v >> 32));
}

/* SDT ヘッダ（36 バイト）と MADT 固有の 8 バイトを書く。長さとチェックサムは finish で埋める */
static void madt_begin(struct madt_builder *b, uint32_t lapic_phys, uint32_t flags)
{
	b->len = 0;
	put8(b, 'A');
	put8(b, 'P');
	put8(b, 'I');
	put8(b, 'C');
	put32(b, 0); /* 長さ（後で埋める） */
	put8(b, 5);  /* revision */
	put8(b, 0);  /* checksum（後で埋める） */
	for (int i = 0; i < 6; i++)
		put8(b, 'O'); /* OEM ID */
	for (int i = 0; i < 8; i++)
		put8(b, 'T'); /* OEM テーブル ID */
	put32(b, 1);          /* OEM リビジョン */
	put32(b, 0x4b554920); /* 作成者 ID */
	put32(b, 1);          /* 作成者リビジョン */
	put32(b, lapic_phys);
	put32(b, flags);
}

static void madt_finish(struct madt_builder *b)
{
	uint8_t sum = 0;

	b->buf[4] = (uint8_t)b->len;
	b->buf[5] = (uint8_t)(b->len >> 8);
	b->buf[6] = 0;
	b->buf[7] = 0;
	b->buf[9] = 0;
	for (size_t i = 0; i < b->len; i++)
		sum = (uint8_t)(sum + b->buf[i]);
	b->buf[9] = (uint8_t)(0x100 - sum);
}

static void add_lapic(struct madt_builder *b, uint8_t uid, uint8_t apic_id, uint32_t flags)
{
	put8(b, 0);
	put8(b, 8);
	put8(b, uid);
	put8(b, apic_id);
	put32(b, flags);
}

static void add_ioapic(struct madt_builder *b, uint8_t id, uint32_t phys, uint32_t gsi_base)
{
	put8(b, 1);
	put8(b, 12);
	put8(b, id);
	put8(b, 0);
	put32(b, phys);
	put32(b, gsi_base);
}

static void add_iso(struct madt_builder *b, uint8_t irq, uint32_t gsi, uint16_t flags)
{
	put8(b, 2);
	put8(b, 10);
	put8(b, 0); /* バス = ISA */
	put8(b, irq);
	put32(b, gsi);
	put16(b, flags);
}

static void add_lapic_override(struct madt_builder *b, uint64_t phys)
{
	put8(b, 5);
	put8(b, 12);
	put16(b, 0);
	put64(b, phys);
}

static void add_x2apic(struct madt_builder *b, uint32_t x2apic_id, uint32_t flags)
{
	put8(b, 9);
	put8(b, 16);
	put16(b, 0);
	put32(b, x2apic_id);
	put32(b, flags);
	put32(b, 0); /* ACPI UID */
}

/* QEMU の q35 に近い典型的な MADT */
static void build_typical(struct madt_builder *b)
{
	madt_begin(b, 0xfee00000, 1);
	add_lapic(b, 0, 0, 1);
	add_lapic(b, 1, 1, 1);
	add_ioapic(b, 0, 0xfec00000, 0);
	add_iso(b, 0, 2, 0);
	add_iso(b, 9, 9, 0x000d); /* 極性 active high(01)、トリガ level(11) */
	madt_finish(b);
}

TEST(acpi_checksum_detects_corruption)
{
	struct madt_builder b;

	build_typical(&b);
	EXPECT(acpi_checksum_ok(b.buf, b.len));
	b.buf[20] ^= 0x01;
	EXPECT(!acpi_checksum_ok(b.buf, b.len));
	EXPECT(acpi_checksum_ok(b.buf, 0)); /* 長さ 0 は合計 0 */
}

TEST(acpi_parse_typical_madt)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	build_typical(&b);
	EXPECT(acpi_parse_madt(b.buf, b.len, &info));
	EXPECT_EQ_U(info.lapic_phys, 0xfee00000);
	EXPECT(info.has_legacy_pic);
	EXPECT_EQ(info.cpu_count, 2);
	EXPECT_EQ(info.cpu_apic_ids[0], 0);
	EXPECT_EQ(info.cpu_apic_ids[1], 1);
	EXPECT_EQ(info.ioapic_count, 1);
	EXPECT_EQ(info.ioapics[0].id, 0);
	EXPECT_EQ_U(info.ioapics[0].phys, 0xfec00000);
	EXPECT_EQ(info.ioapics[0].gsi_base, 0);
	EXPECT_EQ(info.iso_count, 2);
	EXPECT_EQ(info.isos[1].isa_irq, 9);
	EXPECT_EQ(info.isos[1].flags, 0x000d);
}

TEST(acpi_parse_skips_disabled_cpus)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	madt_begin(&b, 0xfee00000, 0);
	add_lapic(&b, 0, 0, 1);
	add_lapic(&b, 1, 7, 0); /* 無効 */
	add_lapic(&b, 2, 3, 2); /* online capable だが今は無効 */
	madt_finish(&b);
	EXPECT(acpi_parse_madt(b.buf, b.len, &info));
	EXPECT_EQ(info.cpu_count, 1);
	EXPECT_EQ(info.cpu_apic_ids[0], 0);
	EXPECT(!info.has_legacy_pic);
}

TEST(acpi_parse_lapic_override_and_x2apic)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	madt_begin(&b, 0xfee00000, 0);
	add_lapic_override(&b, 0x123456789000ull);
	add_x2apic(&b, 5, 1);
	add_x2apic(&b, 0x1000, 1); /* 255 を超える ID は記録できないので捨てる */
	add_x2apic(&b, 6, 0);      /* 無効 */
	madt_finish(&b);
	EXPECT(acpi_parse_madt(b.buf, b.len, &info));
	EXPECT_EQ_U(info.lapic_phys, 0x123456789000ull);
	EXPECT_EQ(info.cpu_count, 1);
	EXPECT_EQ(info.cpu_apic_ids[0], 5);
}

TEST(acpi_parse_skips_unknown_entries)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	madt_begin(&b, 0xfee00000, 0);
	/* 種類 0x7f（未知）、長さ 6 */
	put8(&b, 0x7f);
	put8(&b, 6);
	put32(&b, 0xdeadbeef);
	add_ioapic(&b, 2, 0xfec01000, 24);
	madt_finish(&b);
	EXPECT(acpi_parse_madt(b.buf, b.len, &info));
	EXPECT_EQ(info.ioapic_count, 1);
	EXPECT_EQ(info.ioapics[0].gsi_base, 24);
}

TEST(acpi_parse_rejects_bad_header)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	build_typical(&b);
	/* 範囲が短すぎる */
	EXPECT(!acpi_parse_madt(b.buf, 43, &info));
	EXPECT(!acpi_parse_madt(NULL, 100, &info));
	/* ヘッダの長さが渡された範囲を超える */
	EXPECT(!acpi_parse_madt(b.buf, b.len - 1, &info));
	/* シグネチャが違う */
	b.buf[0] = 'X';
	EXPECT(!acpi_parse_madt(b.buf, b.len, &info));
	/* 失敗しても out はゼロで初期化されている */
	EXPECT_EQ(info.cpu_count, 0);
	EXPECT_EQ(info.ioapic_count, 0);
}

TEST(acpi_parse_rejects_zero_length_entry)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	madt_begin(&b, 0xfee00000, 0);
	add_lapic(&b, 0, 0, 1);
	put8(&b, 0); /* 長さ 0 のエントリ（無限ループの原因になる） */
	put8(&b, 0);
	put32(&b, 0);
	madt_finish(&b);
	EXPECT(!acpi_parse_madt(b.buf, b.len, &info));

	madt_begin(&b, 0xfee00000, 0);
	put8(&b, 1); /* 長さ 1 も同じ */
	put8(&b, 1);
	madt_finish(&b);
	EXPECT(!acpi_parse_madt(b.buf, b.len, &info));
}

TEST(acpi_parse_rejects_truncated_entries)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	/* エントリが表の末尾をはみ出す */
	madt_begin(&b, 0xfee00000, 0);
	put8(&b, 1);
	put8(&b, 12);
	put8(&b, 0);
	put8(&b, 0); /* 4 バイトしか書かずに終わる */
	madt_finish(&b);
	EXPECT(!acpi_parse_madt(b.buf, b.len, &info));

	/* 種類に対して短すぎる長さ（I/O APIC なのに 8 バイト） */
	madt_begin(&b, 0xfee00000, 0);
	put8(&b, 1);
	put8(&b, 8);
	put32(&b, 0);
	put16(&b, 0);
	madt_finish(&b);
	EXPECT(!acpi_parse_madt(b.buf, b.len, &info));

	/* 種類と長さの 2 バイトすら揃っていない（末尾に 1 バイトだけ） */
	madt_begin(&b, 0xfee00000, 0);
	put8(&b, 0);
	madt_finish(&b);
	EXPECT(!acpi_parse_madt(b.buf, b.len, &info));
}

TEST(acpi_parse_caps_overflowing_entries)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	madt_begin(&b, 0xfee00000, 0);
	for (int i = 0; i < ACPI_MAX_CPUS + 5; i++)
		add_lapic(&b, (uint8_t)i, (uint8_t)i, 1);
	for (int i = 0; i < ACPI_MAX_IOAPICS + 2; i++)
		add_ioapic(&b, (uint8_t)i, 0xfec00000u + 0x1000u * (uint32_t)i, 24u * (uint32_t)i);
	for (int i = 0; i < ACPI_MAX_ISOS + 3; i++)
		add_iso(&b, (uint8_t)i, (uint32_t)i + 100, 0);
	madt_finish(&b);
	/* 上限を超えた分は捨てるが、表としては正しいので成功する */
	EXPECT(acpi_parse_madt(b.buf, b.len, &info));
	EXPECT_EQ(info.cpu_count, ACPI_MAX_CPUS);
	EXPECT_EQ(info.ioapic_count, ACPI_MAX_IOAPICS);
	EXPECT_EQ(info.iso_count, ACPI_MAX_ISOS);
	EXPECT_EQ(info.cpu_apic_ids[ACPI_MAX_CPUS - 1], ACPI_MAX_CPUS - 1);
}

TEST(acpi_isa_irq_to_gsi_uses_overrides)
{
	struct madt_builder b;
	struct acpi_madt_info info;
	uint16_t flags = 0xffff;

	build_typical(&b);
	EXPECT(acpi_parse_madt(b.buf, b.len, &info));
	/* IRQ 0（PIT）は GSI 2 へ上書きされている */
	EXPECT_EQ(acpi_isa_irq_to_gsi(&info, 0, &flags), 2);
	EXPECT_EQ(flags, 0);
	EXPECT_EQ(acpi_isa_irq_to_gsi(&info, 9, &flags), 9);
	EXPECT_EQ(flags, 0x000d);
	/* 上書きがない IRQ は同じ番号、flags は 0 */
	flags = 0xffff;
	EXPECT_EQ(acpi_isa_irq_to_gsi(&info, 1, &flags), 1);
	EXPECT_EQ(flags, 0);
	/* NULL を渡しても動く */
	EXPECT_EQ(acpi_isa_irq_to_gsi(NULL, 4, NULL), 4);
	EXPECT_EQ(acpi_isa_irq_to_gsi(&info, 0, NULL), 2);
}

TEST(acpi_parse_counts_duplicate_cpu_once)
{
	struct madt_builder b;
	struct acpi_madt_info info;

	/* 同じ CPU（APIC ID 1）を Local APIC と x2APIC の両方の型で書くファームウェアがある */
	madt_begin(&b, 0xfee00000, 1);
	add_lapic(&b, 0, 0, 1);
	add_lapic(&b, 1, 1, 1);
	add_x2apic(&b, 1, 1);
	add_x2apic(&b, 2, 1);
	madt_finish(&b);
	EXPECT(acpi_parse_madt(b.buf, b.len, &info));
	EXPECT_EQ(info.cpu_count, 3);
	EXPECT_EQ(info.cpu_apic_ids[0], 0);
	EXPECT_EQ(info.cpu_apic_ids[1], 1);
	EXPECT_EQ(info.cpu_apic_ids[2], 2);
}

/* 再現できるように固定シードで回す疑似乱数（xorshift32） */
static uint32_t fuzz_next(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

/*
 * 正しい MADT を乱数で壊して（バイトの書き換え、長さの欄の書き換え、末尾の切り詰め）
 * 解析させる。渡した範囲の外を 1 バイトでも読めば AddressSanitizer が止めるよう、
 * 入力はちょうどの大きさで malloc した領域に置く。成功した場合も、数が上限を超えないこと。
 */
TEST(acpi_parse_survives_random_corruption)
{
	struct madt_builder b;
	uint32_t state = 0x4b554931; /* "KUI1" */

	madt_begin(&b, 0xfee00000, 1);
	add_lapic(&b, 0, 0, 1);
	add_lapic(&b, 1, 1, 1);
	add_ioapic(&b, 0, 0xfec00000, 0);
	add_iso(&b, 0, 2, 0);
	add_iso(&b, 9, 9, 0x000d);
	add_lapic_override(&b, 0xfee00000);
	add_x2apic(&b, 300, 1);
	madt_finish(&b);

	for (int round = 0; round < 20000; round++) {
		size_t len = b.len;
		uint8_t *buf;
		struct acpi_madt_info info;
		int edits = 1 + (int)(fuzz_next(&state) % 4);

		/* 4 回に 1 回は末尾を切り詰める */
		if (fuzz_next(&state) % 4 == 0)
			len = fuzz_next(&state) % (b.len + 1);
		buf = malloc(len ? len : 1);
		EXPECT(buf != NULL);
		if (buf == NULL)
			return;
		memcpy(buf, b.buf, len);
		for (int i = 0; i < edits && len > 0; i++) {
			size_t pos = fuzz_next(&state) % len;

			/* エントリの長さの欄やヘッダの長さの欄も、たまたま当たれば壊れる */
			buf[pos] = (uint8_t)fuzz_next(&state);
		}
		if (acpi_parse_madt(buf, len, &info)) {
			EXPECT(info.cpu_count <= ACPI_MAX_CPUS);
			EXPECT(info.ioapic_count <= ACPI_MAX_IOAPICS);
			EXPECT(info.iso_count <= ACPI_MAX_ISOS);
		}
		free(buf);
	}
}
