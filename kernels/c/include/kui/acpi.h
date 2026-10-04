/*
 * ACPI テーブルの読み取り（M1 では割り込みコントローラの情報＝MADT のみ）。
 *
 * RSDP → XSDT（または RSDT）→ 各テーブル、の順に辿る。
 * MADT（シグネチャ "APIC"）から、Local APIC と I/O APIC の場所、
 * ISA の IRQ 番号と I/O APIC の入力番号（GSI）の対応の上書き（ISO）を取り出す。
 *
 * テーブルの解析部分（acpi_parse_*）はメモリ上のバイト列だけを見るので、
 * ホスト上の単体テストで、作り物のテーブルを使って検証できる。
 */
#ifndef KUI_ACPI_H
#define KUI_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/boot_info.h>

#define ACPI_MAX_IOAPICS 8
#define ACPI_MAX_ISOS    16
#define ACPI_MAX_CPUS    64

struct acpi_ioapic {
	uint8_t id;
	uint32_t phys;     /* MMIO の物理アドレス */
	uint32_t gsi_base; /* この I/O APIC の入力 0 番が担当する GSI */
};

/* ISA IRQ の上書き（Interrupt Source Override） */
struct acpi_iso {
	uint8_t isa_irq;
	uint32_t gsi;
	uint16_t flags; /* MPS INTI flags: bit0-1 極性、bit2-3 トリガモード */
};

struct acpi_madt_info {
	uint64_t lapic_phys; /* Local APIC の物理アドレス（64bit 上書きがあれば反映済み） */
	bool has_legacy_pic; /* 8259 PIC が存在する（MADT の flags bit0） */
	size_t cpu_count;    /* 有効な Local APIC の数 */
	uint8_t cpu_apic_ids[ACPI_MAX_CPUS];
	size_t ioapic_count;
	struct acpi_ioapic ioapics[ACPI_MAX_IOAPICS];
	size_t iso_count;
	struct acpi_iso isos[ACPI_MAX_ISOS];
};

/* len バイトの合計が 0 なら true（ACPI のチェックサム規則） */
bool acpi_checksum_ok(const void *data, size_t len);

/*
 * MADT 本体（ヘッダを含む先頭から length バイト）を解析して out を埋める。
 * 形式が壊れていれば false。上限を超えた項目は捨てる（false にはしない）。
 */
bool acpi_parse_madt(const void *madt, size_t length, struct acpi_madt_info *out);

/*
 * ISA IRQ を GSI に変換する。ISO があればそれに従い、なければ同じ番号。
 * flags_out が NULL でなければ ISO の flags（なければ 0）を入れる。
 */
uint32_t acpi_isa_irq_to_gsi(const struct acpi_madt_info *info, uint8_t isa_irq,
			     uint16_t *flags_out);

/*
 * boot_info の RSDP から MADT を探して解析する。成功すれば true。
 * 表示: "acpi: MADT: <cpu 数> cpus, <ioapic 数> ioapics, <iso 数> overrides"
 */
bool acpi_init(const struct boot_info *info);

/* acpi_init() の結果。未初期化・失敗なら NULL */
const struct acpi_madt_info *acpi_madt(void);

#endif
