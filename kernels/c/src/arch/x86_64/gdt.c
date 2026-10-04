/*
 * GDT（Global Descriptor Table）と TSS（Task State Segment）。
 *
 * 64 ビットモードではセグメントの「基底アドレスと上限」はほぼ使われないが、
 * 次の目的で GDT は今でも必要になる:
 *   - コードセグメントの L ビット（64 ビットコード）と特権レベル（ring 0 / ring 3）
 *   - TSS の場所を CPU に教える（TSS は GDT の中の記述子で指す）
 *
 * Limine が用意した GDT は「ブートローダー用メモリ」にあり、後で再利用する予定なので、
 * カーネル自身の GDT を作って読み込み直す。
 *
 * セレクタ（GDT の中の位置 × 8 + 特権レベル）の並びは、M4 で使う syscall / sysret 命令の
 * 決まりに合わせてある:
 *   - syscall は  CS = STAR[47:32]、SS = STAR[47:32] + 8 にする
 *       → カーネルのコード 0x08、データ 0x10 を隣り合わせに置く
 *   - sysret（64 ビット）は CS = STAR[63:48] + 16、SS = STAR[63:48] + 8 にする
 *       → STAR[63:48] = 0x10 とすると、ユーザーのデータ 0x18、コード 0x20 の順になる
 *   この「ユーザーはデータが先、コードが後」という並びは sysret の都合による。
 *
 *   0x00 null
 *   0x08 カーネル コード（64 ビット、ring 0）
 *   0x10 カーネル データ（ring 0）
 *   0x18 ユーザー データ（ring 3）  セレクタ値 0x1b
 *   0x20 ユーザー コード（64 ビット、ring 3）  セレクタ値 0x23
 *   0x28 TSS（64 ビットモードでは 16 バイト = 2 エントリ分）
 */
#include <stddef.h>
#include <stdint.h>

#include <kui/arch/x86_64/gdt.h>
#include <kui/arch/x86_64/interrupt.h>
#include <kui/printk.h>
#include <kui/string.h>

/*
 * 64 ビットの TSS。64 ビットモードではタスク切り替えには使わず、
 *   - rsp0: ユーザーモードから割り込まれたときに切り替えるカーネルのスタック（M3/M4 で設定）
 *   - ist1〜7: 特定の例外で必ず切り替える独立したスタック（IST: Interrupt Stack Table）
 * を CPU に教えるためだけに使う。
 */
struct __attribute__((packed)) tss {
	uint32_t reserved0;
	uint64_t rsp[3];
	uint64_t reserved1;
	uint64_t ist[7];
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t iopb_offset;
};

_Static_assert(sizeof(struct tss) == 104, "64 ビット TSS の大きさは 104 バイト");

struct __attribute__((packed)) gdtr {
	uint16_t limit;
	uint64_t base;
};

/*
 * セグメント記述子（8 バイト）の作り方。64 ビットモードで意味があるのは主に
 * アクセスバイト（P, DPL, S, 種類）とフラグ（L: 64 ビットコード、G: 4KiB 単位）。
 *   アクセスバイト: 0x9a = P | S | 実行可 | 読み込み可（ring 0 コード）
 *                   0x92 = P | S | 書き込み可（ring 0 データ）
 *                   0xfa / 0xf2 = 上の DPL を 3 にしたもの
 *   フラグ: 0xa = G | L（64 ビットコード）、0xc = G | DB（データ。64 ビットでは無視される）
 * 基底は 0、上限は 0xfffff（G=1 で 4GiB）。64 ビットモードではどちらも無視される。
 */
#define SEG_DESC(access, flags)                                                                    \
	((0xffffULL) | ((uint64_t)(access) << 40) | (0xfULL << 48) | ((uint64_t)(flags) << 52))

enum {
	GDT_NULL = 0,
	GDT_KERNEL_CODE,
	GDT_KERNEL_DATA,
	GDT_USER_DATA,
	GDT_USER_CODE,
	GDT_TSS_LOW,
	GDT_TSS_HIGH,
	GDT_ENTRIES,
};

_Static_assert(GDT_KERNEL_CODE * 8 == GDT_SEL_KERNEL_CODE, "セレクタの定義と GDT の並びの一致");
_Static_assert(GDT_KERNEL_DATA * 8 == GDT_SEL_KERNEL_DATA, "セレクタの定義と GDT の並びの一致");
_Static_assert(GDT_USER_DATA * 8 + 3 == GDT_SEL_USER_DATA, "セレクタの定義と GDT の並びの一致");
_Static_assert(GDT_USER_CODE * 8 + 3 == GDT_SEL_USER_CODE, "セレクタの定義と GDT の並びの一致");
_Static_assert(GDT_TSS_LOW * 8 == GDT_SEL_TSS, "セレクタの定義と GDT の並びの一致");

static uint64_t gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct tss tss __attribute__((aligned(16)));

/*
 * IST 用のスタック。どれも「通常のスタックが信用できない」ときに使う:
 *   IST1: #DF（ダブルフォールト）。スタックあふれで #PF が起きると、通常のスタックには
 *         例外の情報を積めず #DF になる。別のスタックがないと #DF も処理できず、
 *         CPU はトリプルフォールトでリセットしてしまう
 *   IST2: NMI（マスク不可能割り込み）。どんな瞬間にも来うるため
 *   IST3: #MC（マシンチェック）。ハードウェアの重大な異常
 */
#define IST_STACK_SIZE (16 * 1024)

static uint8_t ist_stacks[3][IST_STACK_SIZE] __attribute__((aligned(16)));

static uint64_t tss_descriptor_low(uint64_t base, uint32_t limit)
{
	/* 種類 0x9 = 「使用可能な 64 ビット TSS」、P=1、DPL=0 → アクセスバイト 0x89 */
	return (limit & 0xffffULL) | ((base & 0xffffffULL) << 16) | (0x89ULL << 40) |
	       ((uint64_t)((limit >> 16) & 0xf) << 48) | (((base >> 24) & 0xffULL) << 56);
}

void gdt_init(void)
{
	struct gdtr gdtr;

	memset(&tss, 0, sizeof(tss));
	/* スタックは高いアドレスから低いアドレスへ伸びるので、末尾の番地を渡す */
	for (int i = 0; i < 3; i++)
		tss.ist[i] = (uint64_t)(uintptr_t)&ist_stacks[i][IST_STACK_SIZE];
	/* I/O 許可ビットマップは使わない（TSS の大きさ以上を指すと「なし」の意味になる） */
	tss.iopb_offset = sizeof(tss);

	gdt[GDT_NULL] = 0;
	gdt[GDT_KERNEL_CODE] = SEG_DESC(0x9a, 0xa);
	gdt[GDT_KERNEL_DATA] = SEG_DESC(0x92, 0xc);
	gdt[GDT_USER_DATA] = SEG_DESC(0xf2, 0xc);
	gdt[GDT_USER_CODE] = SEG_DESC(0xfa, 0xa);
	gdt[GDT_TSS_LOW] = tss_descriptor_low((uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);
	/* 64 ビットの TSS 記述子は 16 バイト。後半には基底アドレスの上位 32 ビットを入れる */
	gdt[GDT_TSS_HIGH] = (uint64_t)(uintptr_t)&tss >> 32;

	gdtr.limit = sizeof(gdt) - 1;
	gdtr.base = (uint64_t)(uintptr_t)gdt;

	/*
	 * GDT を読み込んだ後、セグメントレジスタに新しいセレクタを入れ直す。
	 * セグメントレジスタは記述子の内容を内部に覚えているので、入れ直すまで古いままになる。
	 * CS は mov で直接変えられないので、「戻り先の CS と RIP」を積んで lretq（far return）で
	 * 変える。FS と GS は 64 ビットモードでは基底アドレスを MSR で設定するので 0 にしておく。
	 */
	__asm__ volatile("lgdt %0\n\t"
			 "movw %w1, %%ax\n\t"
			 "movw %%ax, %%ds\n\t"
			 "movw %%ax, %%es\n\t"
			 "movw %%ax, %%ss\n\t"
			 "xorl %%eax, %%eax\n\t"
			 "movw %%ax, %%fs\n\t"
			 "movw %%ax, %%gs\n\t"
			 "pushq %q2\n\t"
			 "leaq 1f(%%rip), %%rax\n\t"
			 "pushq %%rax\n\t"
			 "lretq\n"
			 "1:"
			 :
			 : "m"(gdtr), "r"((uint64_t)GDT_SEL_KERNEL_DATA),
			   "r"((uint64_t)GDT_SEL_KERNEL_CODE)
			 : "rax", "memory");

	/* TSS を読み込む（ltr）。以後、CPU は IST や rsp0 をこの TSS から読む */
	__asm__ volatile("ltr %w0" : : "r"((uint16_t)GDT_SEL_TSS) : "memory");

	printk("gdt: loaded\n");
}

void gdt_set_kernel_stack(uint64_t rsp0)
{
	tss.rsp[0] = rsp0;
}
