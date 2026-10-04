/*
 * ACPI テーブルの探索（仕様は kui/acpi.h を参照）。
 *
 * 辿り方:
 *   RSDP（Root System Description Pointer）: ファームウェアが用意した「入口」。
 *     revision 0（ACPI 1.0）なら RSDT（32bit ポインタの表）の場所だけを持ち、
 *     revision 2 以上なら XSDT（64bit ポインタの表）の場所も持つ。
 *   RSDT / XSDT: 各テーブルの物理アドレスの一覧。
 *   各テーブル: 先頭 4 文字のシグネチャで種類が分かる。MADT は "APIC"。
 *
 * テーブルの読み方: ACPI のテーブルはメモリマップの「ACPI reclaimable」「ACPI NVS」
 * などの領域にある。Limine の base revision 4 以降では、これらの領域と RSDP が
 * HHDM に割り当て済みであることが保証されているので、phys_to_virt() でそのまま読める。
 * （もし保証のないブートローダーに置き換えた場合は、ここで mmio_map() 相当の
 *  キャッシュ有効な割り当てをしてから読む必要がある。）
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/acpi.h>
#include <kui/mmio.h>
#include <kui/printk.h>

#define SDT_HEADER_SIZE   36
#define RSDP_V1_SIZE      20 /* revision 0 のチェックサム対象 */
#define RSDP_V2_MIN_SIZE  36
/*
 * 長さの欄の上限（安全装置）。壊れたテーブルの巨大な長さを信じてチェックサムを計算すると、
 * 割り当てのない領域まで読んでページフォルトになる。実際のテーブルはこれよりずっと小さい。
 */
#define RSDP_MAX_SIZE     1024
#define SDT_MAX_SIZE      (1u << 20)

static struct acpi_madt_info madt_info;
static bool madt_valid;

static uint32_t read32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint64_t read64(const uint8_t *p)
{
	return (uint64_t)read32(p) | ((uint64_t)read32(p + 4) << 32);
}

static bool sig_eq(const uint8_t *p, const char *sig, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (p[i] != (uint8_t)sig[i])
			return false;
	}
	return true;
}

/* 物理アドレス phys にある SDT を、シグネチャと長さとチェックサムを確かめてから返す */
static const uint8_t *map_sdt(uint64_t phys, const char *sig)
{
	const uint8_t *p;
	uint32_t len;

	if (phys == 0)
		return NULL;
	p = phys_to_virt(phys);
	if (sig != NULL && !sig_eq(p, sig, 4))
		return NULL;
	len = read32(p + 4);
	if (len < SDT_HEADER_SIZE || len > SDT_MAX_SIZE || !acpi_checksum_ok(p, len))
		return NULL;
	return p;
}

/* RSDT / XSDT を走査して、シグネチャ sig のテーブルを探す */
static const uint8_t *find_table(const uint8_t *root, bool xsdt, const char *sig)
{
	uint32_t len = read32(root + 4);
	size_t entry_size = xsdt ? 8 : 4;
	size_t count = (len - SDT_HEADER_SIZE) / entry_size;

	for (size_t i = 0; i < count; i++) {
		const uint8_t *e = root + SDT_HEADER_SIZE + i * entry_size;
		uint64_t phys = xsdt ? read64(e) : read32(e);
		const uint8_t *t;

		if (phys == 0)
			continue;
		/* シグネチャだけ先に見る（チェックサムの計算は一致したものだけにする） */
		if (!sig_eq(phys_to_virt(phys), sig, 4))
			continue;
		t = map_sdt(phys, sig);
		if (t != NULL)
			return t;
		printk("acpi: table %c%c%c%c at 0x%llx has a bad checksum\n", sig[0], sig[1],
		       sig[2], sig[3], (unsigned long long)phys);
	}
	return NULL;
}

bool acpi_init(const struct boot_info *info)
{
	const uint8_t *rsdp, *root = NULL, *madt;
	bool xsdt = false;

	madt_valid = false;
	if (info->rsdp_phys == 0) {
		printk("acpi: no RSDP\n");
		return false;
	}

	rsdp = phys_to_virt(info->rsdp_phys);
	if (!sig_eq(rsdp, "RSD PTR ", 8) || !acpi_checksum_ok(rsdp, RSDP_V1_SIZE)) {
		printk("acpi: bad RSDP\n");
		return false;
	}

	/* revision 2 以上なら XSDT を優先する（拡張チェックサムは全体の長さで検証） */
	if (rsdp[15] >= 2) {
		uint32_t rsdp_len = read32(rsdp + 20);

		if (rsdp_len >= RSDP_V2_MIN_SIZE && rsdp_len <= RSDP_MAX_SIZE &&
		    acpi_checksum_ok(rsdp, rsdp_len)) {
			root = map_sdt(read64(rsdp + 24), "XSDT");
			xsdt = root != NULL;
		}
	}
	if (root == NULL)
		root = map_sdt(read32(rsdp + 16), "RSDT");
	if (root == NULL) {
		printk("acpi: no valid RSDT/XSDT\n");
		return false;
	}

	madt = find_table(root, xsdt, "APIC");
	if (madt == NULL) {
		printk("acpi: MADT not found\n");
		return false;
	}
	if (!acpi_parse_madt(madt, read32(madt + 4), &madt_info)) {
		printk("acpi: MADT is malformed\n");
		return false;
	}
	madt_valid = true;
	printk("acpi: MADT: %zu cpus, %zu ioapics, %zu overrides\n", madt_info.cpu_count,
	       madt_info.ioapic_count, madt_info.iso_count);
	return true;
}

const struct acpi_madt_info *acpi_madt(void)
{
	return madt_valid ? &madt_info : NULL;
}
