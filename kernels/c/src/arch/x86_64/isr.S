/*
 * 割り込み・例外の入口（x86_64）。
 *
 * CPU は割り込みを受けると、ベクタ番号に対応する IDT のエントリが指すアドレスへ跳ぶ。
 * そのとき CPU がスタックに積むのは SS, RSP, RFLAGS, CS, RIP（と、一部の例外では
 * エラーコード）だけで、「何番のベクタか」は積まない。そこでベクタごとに小さな入口
 * （スタブ）を 256 個用意し、各スタブが
 *   1. エラーコードを積まない例外・割り込みなら、形をそろえるためにダミーの 0 を積む
 *   2. 自分のベクタ番号を積む
 *   3. 共通の処理 isr_common へ跳ぶ
 * を行う。isr_common は汎用レジスタを全部積んで struct interrupt_frame
 * （kui/arch/x86_64/interrupt.h）の形を作り、C の interrupt_dispatch() を呼ぶ。
 *
 * スタブはすべて 16 バイト境界に置く（.balign 16）。ベクタ n のスタブの番地は
 * isr_stubs_start + 16 * n で求まるので、番地の表を別に作らなくてよい。
 * 各スタブは最大 12 バイト（push 2 + push 5 + jmp 5）なので 16 バイトに収まる。
 * idt.c が isr_stubs_end - isr_stubs_start == 256 * 16 を確認する。
 */

	.section .note.GNU-stack, "", @progbits

	.text

/*
 * 共通の処理。ここに来た時点のスタック（上が低いアドレス）:
 *   vector, error_code, rip, cs, rflags, rsp, ss
 */
	.balign 16
isr_common:
	/* struct interrupt_frame の並び（r15 が一番低いアドレス）になるよう、逆順に積む */
	pushq %rax
	pushq %rbx
	pushq %rcx
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %rbp
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	/*
	 * C の関数は「文字列命令の向き（DF）が 0」であることを前提にする。
	 * 割り込まれた側が DF=1 にしていた可能性があるので戻しておく（RFLAGS は iretq で戻る）。
	 */
	cld

	/*
	 * スタックの 16 バイト境界: 64 ビットモードの CPU は割り込み時に RSP を 16 バイト境界に
	 * そろえてから 5 個（40 バイト）積む。そこへエラーコード（またはダミー）とベクタ番号で
	 * 16 バイト、汎用レジスタ 15 個で 120 バイト、合計 176 = 16 * 11 バイト。
	 * よってここで RSP は 16 バイト境界にあり、System V ABI の呼び出し規約を満たす。
	 */
	movq %rsp, %rdi
	call interrupt_dispatch

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rbp
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax

	/* ベクタ番号とエラーコードを捨てる */
	addq $16, %rsp
	iretq

/*
 * CPU 自身がエラーコードを積む例外:
 *   #DF(8) #TS(10) #NP(11) #SS(12) #GP(13) #PF(14) #AC(17) #CP(21) #VC(29) #SX(30)
 * これら以外ではダミーの 0 を積む。
 */
.macro ISR_STUB vec
	.balign 16
	.if (\vec == 8) || (\vec == 10) || (\vec == 11) || (\vec == 12) || (\vec == 13) || (\vec == 14) || (\vec == 17) || (\vec == 21) || (\vec == 29) || (\vec == 30)
	.else
	pushq $0
	.endif
	pushq $\vec
	jmp isr_common
.endm

	.balign 16
	.globl isr_stubs_start
isr_stubs_start:
	.set isr_vec, 0
	.rept 256
	ISR_STUB isr_vec
	.set isr_vec, isr_vec + 1
	.endr
	.balign 16
	.globl isr_stubs_end
isr_stubs_end:
