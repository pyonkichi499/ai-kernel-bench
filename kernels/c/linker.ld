/*
 * Kui のリンカスクリプト。
 *
 * カーネルを仮想アドレス空間の最上位 2GiB（0xffffffff80000000 以降）に配置する。
 * この範囲は -mcmodel=kernel で生成したコードが前提とする場所で、Limine もここへ読み込む。
 *
 * 実行ファイルを 3 つのセグメント（プログラムヘッダ）に分け、それぞれに権限を付ける:
 *   text   : 実行可・読み込み専用（コード）
 *   rodata : 読み込み専用（定数、文字列、Limine リクエスト以外の読み取り専用データ）
 *   data   : 読み書き可（変数、Limine リクエスト、.bss）
 * Limine はこの権限どおりにページを割り当てるので、コードの書き換えやデータの実行を防げる。
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
	text   PT_LOAD FLAGS(5); /* R-X */
	rodata PT_LOAD FLAGS(4); /* R-- */
	data   PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
	. = 0xffffffff80000000;

	__kernel_start = .;

	.text : {
		*(.text .text.*)
	} :text

	/* セグメントの境界はページ（4KiB）単位にそろえる。権限はページ単位でしか付けられないため */
	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.rodata : {
		*(.rodata .rodata.*)
	} :rodata

	/*
	 * カーネル内単体テストの記述子。kui/ktest.h の KTEST() がここへ置く。
	 * 先頭と末尾に印を付け、ktest_run_all() がその間を走査する。
	 */
	.ktests : ALIGN(8) {
		__ktests_start = .;
		KEEP(*(.ktests))
		__ktests_end = .;
	} :rodata

	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.data : {
		/*
		 * Limine へのリクエスト。Limine はレスポンスへのポインタを書き込むので、
		 * 書き込み可能な領域に置く。開始マーカー、リクエスト、終了マーカーの順に並べる。
		 */
		KEEP(*(.limine_requests_start))
		KEEP(*(.limine_requests))
		KEEP(*(.limine_requests_end))

		*(.data .data.*)
	} :data

	/* 0 で初期化される変数。ファイルには場所を取らず、読み込み時に 0 で埋められる */
	.bss : {
		*(.bss .bss.*)
		*(COMMON)
	} :data

	__kernel_end = .;

	/* 不要なセクションは捨てる */
	/DISCARD/ : {
		*(.eh_frame*)
		*(.note .note.*)
		*(.comment)
	}
}
