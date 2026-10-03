/*
 * Limine ブートプロトコルの変換層。
 *
 * Limine は、カーネルのバイナリの中に置かれた「リクエスト」構造体を起動前に探し、
 * 対応する「レスポンス」へのポインタを書き込んでからカーネルへジャンプする。
 * このファイルはそのリクエストを宣言し、受け取った情報をブートローダーに
 * 依存しない struct boot_info へ詰め替えて kmain() を呼ぶ。
 *
 * カーネル本体で Limine を知っているのはこのファイルだけにする。
 * ブートローダーを差し替えるときは、このファイルの代わりを書けばよい。
 */
#include <stdint.h>

#include <limine.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/boot_info.h>
#include <kui/printk.h>
#include <kui/serial.h>
#include <kui/string.h>

/*
 * リクエストは専用セクションに置き、リンカスクリプトで開始マーカーと終了マーカーの
 * 間に並べる。Limine はマーカーの間だけを探す。
 * used 属性は「参照されていなくても消さない」ための指定。volatile は、Limine が
 * 書き込んだレスポンスをコンパイラが「変わらない値」として最適化しないための指定。
 */
#define LIMINE_REQUEST __attribute__((used, section(".limine_requests"))) static volatile

__attribute__((used, section(".limine_requests_start"))) static volatile uint64_t
	limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

/* 対応している最新のベースリビジョン 6 を要求する */
LIMINE_REQUEST uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

LIMINE_REQUEST struct limine_bootloader_info_request bootloader_info_request = {
	.id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
	.revision = 0,
};

LIMINE_REQUEST struct limine_executable_cmdline_request cmdline_request = {
	.id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
	.revision = 0,
};

LIMINE_REQUEST struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST_ID,
	.revision = 0,
};

LIMINE_REQUEST struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST_ID,
	.revision = 0,
};

LIMINE_REQUEST struct limine_executable_address_request executable_address_request = {
	.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
	.revision = 0,
};

LIMINE_REQUEST struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST_ID,
	.revision = 0,
};

LIMINE_REQUEST struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
	.revision = 0,
};

__attribute__((
	used,
	section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[] =
	LIMINE_REQUESTS_END_MARKER;

/* kmain へ渡す情報。ブートローダーのメモリに依存しないよう静的領域に持つ */
static struct boot_info boot_info;

const struct boot_info *boot_info_get(void)
{
	return &boot_info;
}

/* Limine のメモリ種別を、カーネル独自の enum mem_type に対応付ける */
static enum mem_type convert_mem_type(uint64_t type)
{
	switch (type) {
	case LIMINE_MEMMAP_USABLE:
		return MEM_USABLE;
	case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
		return MEM_ACPI_RECLAIMABLE;
	case LIMINE_MEMMAP_ACPI_NVS:
		return MEM_ACPI_NVS;
	case LIMINE_MEMMAP_BAD_MEMORY:
		return MEM_BAD;
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		return MEM_BOOTLOADER_RECLAIMABLE;
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
		return MEM_KERNEL_AND_MODULES;
	case LIMINE_MEMMAP_FRAMEBUFFER:
		return MEM_FRAMEBUFFER;
	case LIMINE_MEMMAP_RESERVED:
	case LIMINE_MEMMAP_RESERVED_MAPPED:
	default:
		/* 知らない種類は安全側に倒して「使ってはいけない」とみなす */
		return MEM_RESERVED;
	}
}

/* 起動を続けられない致命的な問題。kmain 前なので panic() と同じ書式で出して止まる */
_Noreturn static void boot_fail(const char *msg)
{
	printk("PANIC: boot: %s\n", msg);
	cpu_halt_forever();
}

static void fill_boot_info(struct boot_info *info)
{
	/* HHDM は他の変換（RSDP、フレームバッファ）に使うので最初に取得する */
	if (hhdm_request.response == NULL)
		boot_fail("no HHDM response from bootloader");
	info->hhdm_offset = hhdm_request.response->offset;

	if (bootloader_info_request.response != NULL) {
		struct limine_bootloader_info_response *r = bootloader_info_request.response;
		char *p = info->bootloader;
		size_t room = sizeof(info->bootloader);
		size_t n = strlcpy(p, r->name, room);

		/* "名前 バージョン" の形にする（収まらない分は切り詰める） */
		if (n + 1 < room) {
			p[n] = ' ';
			strlcpy(p + n + 1, r->version, room - n - 1);
		}
	} else {
		strlcpy(info->bootloader, "unknown", sizeof(info->bootloader));
	}

	if (cmdline_request.response != NULL && cmdline_request.response->cmdline != NULL)
		strlcpy(info->cmdline, cmdline_request.response->cmdline, sizeof(info->cmdline));
	else
		info->cmdline[0] = '\0';

	if (executable_address_request.response == NULL)
		boot_fail("no executable address response from bootloader");
	info->kernel_phys_base = executable_address_request.response->physical_base;
	info->kernel_virt_base = executable_address_request.response->virtual_base;

	/* ベースリビジョン 4 以降、RSDP は HHDM 上の仮想アドレスで渡されるので物理に戻す */
	if (rsdp_request.response != NULL && rsdp_request.response->address != NULL)
		info->rsdp_phys = (uint64_t)rsdp_request.response->address - info->hhdm_offset;
	else
		info->rsdp_phys = 0;

	info->framebuffer.present = 0;
	if (framebuffer_request.response != NULL &&
	    framebuffer_request.response->framebuffer_count > 0) {
		struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

		info->framebuffer.present = 1;
		/* フレームバッファのアドレスも HHDM 上の仮想アドレスなので物理に戻す */
		info->framebuffer.phys = (uint64_t)fb->address - info->hhdm_offset;
		info->framebuffer.width = fb->width;
		info->framebuffer.height = fb->height;
		info->framebuffer.pitch = fb->pitch;
		info->framebuffer.bpp = fb->bpp;
	}

	if (memmap_request.response == NULL)
		boot_fail("no memory map response from bootloader");
	{
		struct limine_memmap_response *r = memmap_request.response;
		uint64_t count = r->entry_count;

		if (count > BOOT_INFO_MAX_MEMMAP) {
			printk("boot: memory map has %llu entries, keeping first %d\n",
			       (unsigned long long)count, BOOT_INFO_MAX_MEMMAP);
			count = BOOT_INFO_MAX_MEMMAP;
		}
		for (uint64_t i = 0; i < count; i++) {
			info->memmap[i].base = r->entries[i]->base;
			info->memmap[i].length = r->entries[i]->length;
			info->memmap[i].type = convert_mem_type(r->entries[i]->type);
		}
		info->memmap_count = (size_t)count;
	}
}

_Noreturn void _start(void);

/*
 * カーネルの入口。Limine は 64 ビットモード・ページング有効・割り込み禁止の状態で、
 * 64KiB 以上のスタックを用意してここへジャンプしてくる。
 */
_Noreturn void _start(void)
{
	/* 何が起きても報告できるよう、まずシリアルを使えるようにする */
	serial_init();

	if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
		boot_fail("bootloader does not support Limine base revision 6");

	fill_boot_info(&boot_info);
	kmain(&boot_info);
}
