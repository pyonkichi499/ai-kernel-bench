/*
 * boot_info: ブートローダーからカーネル本体へ渡す情報。
 *
 * カーネル本体はこの構造体だけを見て動き、Limine などの特定のブートローダーには
 * 直接依存しない。ブートローダーごとの差分は src/boot/ 以下の変換層が吸収する。
 * 将来ブートローダーを自作に置き換えるときも、この形式を埋めて kmain() を呼べばよい。
 *
 * 方針:
 *   - アドレスは原則「物理アドレス」で持つ。仮想アドレスへの変換方法（hhdm_offset）は
 *     ブートローダー独自の約束事なので、暗黙にせず明示的にここへ書く。
 *   - ブートローダーの作ったデータ（メモリマップ等）は、ブートローダー用メモリを
 *     後で再利用しても壊れないよう、カーネル側の静的領域へコピーして持つ。
 */
#ifndef KUI_BOOT_INFO_H
#define KUI_BOOT_INFO_H

#include <stddef.h>
#include <stdint.h>

/* メモリ領域の種類。値は Limine と独立に定義する（変換層で対応付ける）。 */
enum mem_type {
	MEM_USABLE = 0,             /* 自由に使ってよい RAM */
	MEM_RESERVED,               /* 使ってはいけない */
	MEM_ACPI_RECLAIMABLE,       /* ACPI テーブルを読み終えたら再利用できる */
	MEM_ACPI_NVS,               /* ファームウェアが使い続ける。触らない */
	MEM_BAD,                    /* 壊れたメモリ */
	MEM_BOOTLOADER_RECLAIMABLE, /* ブートローダーの領域。boot_info を使い終えたら再利用できる */
	MEM_KERNEL_AND_MODULES,     /* カーネル本体とモジュールが読み込まれている */
	MEM_FRAMEBUFFER,            /* 画面のフレームバッファ */
	MEM_TYPE_COUNT,
};

struct mem_region {
	uint64_t base;   /* 物理アドレス */
	uint64_t length; /* バイト数 */
	enum mem_type type;
};

struct boot_framebuffer {
	int present;    /* 0 なら画面なし */
	uint64_t phys;  /* フレームバッファの物理アドレス */
	uint64_t width; /* ピクセル数 */
	uint64_t height;
	uint64_t pitch; /* 1 行あたりのバイト数 */
	uint16_t bpp;   /* 1 ピクセルあたりのビット数 */
};

#define BOOT_INFO_MAX_MEMMAP  256
#define BOOT_INFO_CMDLINE_MAX 256
#define BOOT_INFO_NAME_MAX    64

struct boot_info {
	/* 起動したブートローダーの名前とバージョン（例: "Limine 11.2.1"） */
	char bootloader[BOOT_INFO_NAME_MAX];

	/* カーネルのコマンドライン。空文字列のこともある */
	char cmdline[BOOT_INFO_CMDLINE_MAX];

	/*
	 * 物理メモリの直接マップ（HHDM）の開始アドレス。
	 * 物理アドレス p は仮想アドレス p + hhdm_offset で読み書きできる。
	 * これはブートローダーが用意した仮のページテーブルでの約束であり、
	 * カーネルが自前のページテーブルに切り替える（M2）まで有効。
	 */
	uint64_t hhdm_offset;

	/* カーネル本体が読み込まれた場所 */
	uint64_t kernel_phys_base;
	uint64_t kernel_virt_base;

	/* ACPI の RSDP の物理アドレス。見つからなければ 0 */
	uint64_t rsdp_phys;

	struct boot_framebuffer framebuffer;

	size_t memmap_count;
	struct mem_region memmap[BOOT_INFO_MAX_MEMMAP];
};

/* カーネル本体の入口。変換層が boot_info を埋めてから呼ぶ。戻らない。 */
_Noreturn void kmain(const struct boot_info *info);

/* 変換層が作った boot_info を返す（kmain に渡されたものと同じ）。テストなどで使う */
const struct boot_info *boot_info_get(void);

/* enum mem_type を表示用の文字列にする */
const char *mem_type_name(enum mem_type type);

#endif
