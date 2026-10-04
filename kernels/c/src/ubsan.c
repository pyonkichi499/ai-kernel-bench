/*
 * UBSan（Undefined Behavior Sanitizer）の実行時ハンドラ。
 *
 * -fsanitize=undefined でビルドすると、コンパイラは未定義動作になりうる箇所
 * （符号付き整数のオーバーフロー、配列の範囲外アクセス、境界のずれたポインタなど）に
 * 検査コードを埋め込み、違反を見つけると __ubsan_handle_* を呼ぶ。
 * 通常のプログラムではコンパイラ付属のランタイムがこれを提供するが、カーネルには
 * ないので自前で用意する。引数の形はコンパイラ（compiler-rt）との取り決め（ABI）に従う。
 *
 * カーネルでの未定義動作は、後で原因不明の不具合として現れるので、見つけ次第
 * 場所を表示して panic する。
 *
 * このファイル自体は検査対象から外してビルドする（ハンドラの中で再び検査に
 * 引っかかると無限に再帰するため）。
 */
#include <stdint.h>

#include <kui/arch/x86_64/cpu.h>
#include <kui/panic.h>
#include <kui/printk.h>

/* 違反が起きたソースの位置 */
struct source_location {
	const char *file;
	uint32_t line;
	uint32_t column;
};

/* 違反に関わった型の情報（ここでは名前の表示にだけ使う） */
struct type_descriptor {
	uint16_t kind;
	uint16_t info;
	char name[];
};

/* 多くのハンドラのデータは「位置 + 型」の形をしている */
struct overflow_data {
	struct source_location loc;
	const struct type_descriptor *type;
};

struct shift_out_of_bounds_data {
	struct source_location loc;
	const struct type_descriptor *lhs_type;
	const struct type_descriptor *rhs_type;
};

struct out_of_bounds_data {
	struct source_location loc;
	const struct type_descriptor *array_type;
	const struct type_descriptor *index_type;
};

struct type_mismatch_data_v1 {
	struct source_location loc;
	const struct type_descriptor *type;
	unsigned char log_alignment; /* 必要なアライメントの log2 */
	unsigned char type_check_kind;
};

struct location_only_data {
	struct source_location loc;
};

struct nonnull_arg_data {
	struct source_location loc;
	struct source_location attr_loc;
	int arg_index;
};

struct nonnull_return_data {
	struct source_location attr_loc;
};

struct invalid_builtin_data {
	struct source_location loc;
	unsigned char kind;
};

struct alignment_assumption_data {
	struct source_location loc;
	struct source_location assumption_loc;
	const struct type_descriptor *type;
};

struct float_cast_overflow_data {
	struct source_location loc;
	const struct type_descriptor *from_type;
	const struct type_descriptor *to_type;
};

/* ubsan_report() の実行中かどうか（表示処理の中で再び違反が起きた場合の無限再帰を防ぐ） */
static int ubsan_reporting;

/* 違反を表示して panic する。detail は追加情報（なければ NULL） */
_Noreturn static void ubsan_report(const char *kind, const struct source_location *loc,
				   const char *detail)
{
	/*
	 * 表示の前に割り込みを禁止する。許可したままだと、表示中に届いたタイマー割り込みの
	 * 処理で別の違反が起きたとき、「表示中の再違反」と誤って扱ってしまう。
	 * どのみち直後に panic するので、戻す必要はない。
	 */
	cpu_disable_interrupts();

	/*
	 * 表示（printk → 書式化）の途中で再び違反が起きた。もう一度表示しようとすると
	 * 同じ違反を繰り返すので、表示せずに panic へ進む（panic は二重 panic として扱う）。
	 */
	if (ubsan_reporting++)
		panic("nested UBSan report (%s)", kind);

	if (loc != 0 && loc->file != 0)
		printk("UBSAN: %s at %s:%u:%u\n", kind, loc->file, loc->line, loc->column);
	else
		printk("UBSAN: %s at <unknown>\n", kind);
	if (detail != 0)
		printk("  %s\n", detail);
	panic("undefined behavior detected (%s)", kind);
}

static const char *type_name(const struct type_descriptor *type)
{
	return type != 0 ? type->name : "?";
}

/*
 * 以下がコンパイラから呼ばれるハンドラ。ヘッダで宣言されないので、
 * 警告を避けるためにプロトタイプをここで宣言してから定義する。
 */
#define UBSAN_HANDLER(name, ...)                     \
	void __ubsan_handle_##name(__VA_ARGS__);     \
	void __ubsan_handle_##name(__VA_ARGS__)

UBSAN_HANDLER(add_overflow, void *data, void *lhs, void *rhs)
{
	(void)lhs, (void)rhs;
	ubsan_report("add-overflow", &((struct overflow_data *)data)->loc,
		     type_name(((struct overflow_data *)data)->type));
}

UBSAN_HANDLER(sub_overflow, void *data, void *lhs, void *rhs)
{
	(void)lhs, (void)rhs;
	ubsan_report("sub-overflow", &((struct overflow_data *)data)->loc,
		     type_name(((struct overflow_data *)data)->type));
}

UBSAN_HANDLER(mul_overflow, void *data, void *lhs, void *rhs)
{
	(void)lhs, (void)rhs;
	ubsan_report("mul-overflow", &((struct overflow_data *)data)->loc,
		     type_name(((struct overflow_data *)data)->type));
}

UBSAN_HANDLER(negate_overflow, void *data, void *old)
{
	(void)old;
	ubsan_report("negate-overflow", &((struct overflow_data *)data)->loc,
		     type_name(((struct overflow_data *)data)->type));
}

UBSAN_HANDLER(divrem_overflow, void *data, void *lhs, void *rhs)
{
	(void)lhs, (void)rhs;
	ubsan_report("divrem-overflow", &((struct overflow_data *)data)->loc,
		     "division by zero or INT_MIN / -1");
}

UBSAN_HANDLER(shift_out_of_bounds, void *data, void *lhs, void *rhs)
{
	(void)lhs, (void)rhs;
	ubsan_report("shift-out-of-bounds", &((struct shift_out_of_bounds_data *)data)->loc, 0);
}

UBSAN_HANDLER(out_of_bounds, void *data, void *index)
{
	(void)index;
	ubsan_report("out-of-bounds", &((struct out_of_bounds_data *)data)->loc,
		     type_name(((struct out_of_bounds_data *)data)->array_type));
}

UBSAN_HANDLER(type_mismatch_v1, void *data, void *ptr)
{
	struct type_mismatch_data_v1 *d = data;
	uintptr_t p = (uintptr_t)ptr;
	uintptr_t align = (uintptr_t)1 << d->log_alignment;
	const char *detail;

	if (p == 0)
		detail = "null pointer access";
	else if (d->log_alignment != 0 && (p & (align - 1)) != 0)
		detail = "misaligned pointer access";
	else
		detail = "object too small for its type";
	ubsan_report("type-mismatch", &d->loc, detail);
}

UBSAN_HANDLER(pointer_overflow, void *data, void *base, void *result)
{
	(void)base, (void)result;
	ubsan_report("pointer-overflow", &((struct location_only_data *)data)->loc, 0);
}

UBSAN_HANDLER(load_invalid_value, void *data, void *value)
{
	(void)value;
	ubsan_report("load-invalid-value", &((struct overflow_data *)data)->loc,
		     type_name(((struct overflow_data *)data)->type));
}

UBSAN_HANDLER(builtin_unreachable, void *data)
{
	ubsan_report("builtin-unreachable", &((struct location_only_data *)data)->loc, 0);
}

UBSAN_HANDLER(missing_return, void *data)
{
	ubsan_report("missing-return", &((struct location_only_data *)data)->loc, 0);
}

UBSAN_HANDLER(vla_bound_not_positive, void *data, void *bound)
{
	(void)bound;
	ubsan_report("vla-bound-not-positive", &((struct overflow_data *)data)->loc, 0);
}

UBSAN_HANDLER(nonnull_arg, void *data)
{
	ubsan_report("nonnull-arg", &((struct nonnull_arg_data *)data)->loc, 0);
}

UBSAN_HANDLER(nonnull_return_v1, void *data, void *loc)
{
	(void)data;
	ubsan_report("nonnull-return", (struct source_location *)loc, 0);
}

UBSAN_HANDLER(invalid_builtin, void *data)
{
	ubsan_report("invalid-builtin", &((struct invalid_builtin_data *)data)->loc,
		     "ctz/clz called with zero");
}

UBSAN_HANDLER(alignment_assumption, void *data, void *ptr, void *align, void *offset)
{
	(void)ptr, (void)align, (void)offset;
	ubsan_report("alignment-assumption", &((struct alignment_assumption_data *)data)->loc, 0);
}

UBSAN_HANDLER(function_type_mismatch, void *data, void *function)
{
	(void)function;
	ubsan_report("function-type-mismatch", &((struct overflow_data *)data)->loc, 0);
}

UBSAN_HANDLER(float_cast_overflow, void *data, void *from)
{
	(void)from;
	ubsan_report("float-cast-overflow", &((struct float_cast_overflow_data *)data)->loc, 0);
}
