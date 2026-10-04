/*
 * スタックトレースと、カーネルに埋め込んだシンボル表の検索。
 *
 * フレームポインタの連鎖:
 *   -fno-omit-frame-pointer でビルドした関数は、先頭で
 *       push %rbp        ; 呼び出し元の rbp を保存
 *       mov  %rsp, %rbp  ; 自分のフレームの基準にする
 *   を行う。すると [rbp] = 呼び出し元の rbp、[rbp + 8] = 戻りアドレス となり、
 *   rbp を辿ると呼び出しの履歴が古い方へ順に得られる。
 *   一番外側の _start は Limine から rbp = 0 で呼ばれるので、0 に着いたら終わり。
 *
 * シンボル表:
 *   ビルド時に 2 段階リンクで作る（kernels/c/tools/gen_symtab.py と Makefile を参照）。
 *   関数の開始アドレスの昇順に並んだ配列なので、二分探索で引ける。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/boot_info.h>
#include <kui/printk.h>
#include <kui/stacktrace.h>

#define STACKTRACE_MAX_FRAMES 32

/* フレームとして読む大きさ（保存された rbp と戻りアドレス） */
#define FRAME_SIZE 16

/* linker.ld が定義するカーネル本体の範囲（IST スタックなど静的なスタックはこの中にある） */
extern const char __kernel_start[];
extern const char __kernel_end[];

/*
 * [addr, addr + FRAME_SIZE) を読んでも #PF にならないことが分かっているか。
 *
 * 壊れた rbp（例: スタックを上書きされた）を辿ると、割り当てのないアドレスを読んで #PF になり、
 * 例外の報告の途中で二重 panic してしまう。そこで「確実に割り当てがある」範囲だけを許す:
 *   - カーネル本体（.bss の IST スタックなど）
 *   - 直接マップ（HHDM）上の RAM。Limine は usable・ブートローダー用・カーネル本体の領域を
 *     必ず HHDM に割り当てる（Limine の起動スタックはブートローダー用の領域にある）。
 *     M2 以降も、自前のページテーブルで RAM 全体を直接マップする前提。
 */
static bool frame_is_readable(uint64_t addr)
{
	const struct boot_info *info = boot_info_get();
	uint64_t end = addr + FRAME_SIZE;

	if (end < addr)
		return false;
	if (addr >= (uint64_t)(uintptr_t)__kernel_start && end <= (uint64_t)(uintptr_t)__kernel_end)
		return true;
	if (!info || info->hhdm_offset == 0 || addr < info->hhdm_offset)
		return false;

	addr -= info->hhdm_offset;
	end -= info->hhdm_offset;
	for (size_t i = 0; i < info->memmap_count; i++) {
		const struct mem_region *r = &info->memmap[i];

		if (r->type != MEM_USABLE && r->type != MEM_BOOTLOADER_RECLAIMABLE &&
		    r->type != MEM_KERNEL_AND_MODULES)
			continue;
		if (addr >= r->base && end <= r->base + r->length)
			return true;
	}
	return false;
}

/* gen_symtab.py が生成する表（1 回目のリンクでは件数 0 の仮の表） */
extern const uint64_t kui_symtab_count;
extern const uint64_t kui_symtab_addrs[];
extern const uint64_t kui_symtab_sizes[];
extern const char *const kui_symtab_names[];

const char *symbol_lookup(uint64_t addr, uint64_t *offset)
{
	uint64_t lo = 0, hi = kui_symtab_count;

	/* addr 以下で最大の開始アドレスを持つシンボルを探す */
	while (lo < hi) {
		uint64_t mid = lo + (hi - lo) / 2;

		if (kui_symtab_addrs[mid] <= addr)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo == 0)
		return NULL;
	lo--;

	/*
	 * 大きさが分かっている関数は、その範囲外なら「見つからない」とする。
	 * 大きさ 0（アセンブリで .size を書いていないラベルなど）は、次のシンボルまでとみなす。
	 */
	if (kui_symtab_sizes[lo] != 0 && addr - kui_symtab_addrs[lo] >= kui_symtab_sizes[lo])
		return NULL;

	if (offset)
		*offset = addr - kui_symtab_addrs[lo];
	return kui_symtab_names[lo];
}

/*
 * 1 フレームを表示する。is_return が真なら addr は戻りアドレス。
 *
 * 戻りアドレスは「call 命令の次の命令」を指す。noreturn の関数（panic など）の呼び出しは
 * 関数の最後の命令になることがあり、その場合の戻りアドレスは関数の範囲の外（次の関数の
 * 先頭）を指してしまう。そこで関数名は addr - 1（call 命令の中）で引き、表示するずれは
 * addr を基準にする。
 */
static void print_frame(int n, uint64_t addr, int is_return)
{
	uint64_t offset;
	const char *name = symbol_lookup(is_return ? addr - 1 : addr, &offset);

	if (name)
		printk("    #%d 0x%016llx %s+0x%llx\n", n, (unsigned long long)addr, name,
		       (unsigned long long)(is_return ? offset + 1 : offset));
	else
		printk("    #%d 0x%016llx ?\n", n, (unsigned long long)addr);
}

void stacktrace_print(uint64_t rip, uint64_t rbp)
{
	int n = 0;

	print_frame(n++, rip, 0);
	while (n < STACKTRACE_MAX_FRAMES) {
		const uint64_t *frame = (const uint64_t *)(uintptr_t)rbp;
		uint64_t next_rbp, ret;

		/*
		 * 壊れた rbp を辿ってページフォールトを起こさないよう確認する:
		 *   - 0 なら一番外側に着いた
		 *   - 8 バイト境界にそろい、確実に読める範囲（frame_is_readable）を指すこと
		 */
		if (rbp == 0)
			break;
		if ((rbp & 7) != 0 || !frame_is_readable(rbp)) {
			printk("    (invalid frame pointer 0x%016llx)\n", (unsigned long long)rbp);
			break;
		}

		next_rbp = frame[0];
		ret = frame[1];
		if (ret == 0)
			break;
		print_frame(n++, ret, 1);

		/*
		 * スタックは低いアドレスへ伸びるので、呼び出し元のフレームは必ず高いアドレスにある。
		 * そうでなければ連鎖が壊れている（無限ループも防げる）。
		 */
		if (next_rbp != 0 && next_rbp <= rbp) {
			printk("    (frame pointer chain broken at 0x%016llx)\n",
			       (unsigned long long)next_rbp);
			break;
		}
		rbp = next_rbp;
	}
}

__attribute__((noinline)) void stacktrace_print_current(void)
{
	const uint64_t *frame = __builtin_frame_address(0);

	/* 自分自身は表示せず、呼び出し元（戻り先）から始める */
	stacktrace_print(frame[1], frame[0]);
}
