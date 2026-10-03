/*
 * printf 形式の書式化エンジン（仕様は kui/format.h を参照）。
 *
 * ハードウェアにも kui/string.h にも依存しないので、ホスト上の単体テストで
 * そのまま検証できる。
 */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kui/format.h>

/* 書式化中の状態 */
struct out {
	format_sink sink;
	void *ctx;
	int count; /* sink へ渡した文字数 */
};

static void put(struct out *o, char c)
{
	o->sink(c, o->ctx);
	o->count++;
}

static void put_repeat(struct out *o, char c, int n)
{
	while (n-- > 0)
		put(o, c);
}

/*
 * 幅と精度の上限。これより大きな指定はこの値に丸める。
 * 異常に大きな指定で計算や出力文字数（int）があふれないようにするため。
 */
#define FORMAT_FIELD_MAX 100000

/* 幅・精度の途中計算値を上限で丸める。呼び出し側は FORMAT_FIELD_MAX * 10 + 9 以下しか渡さない */
static int clamp_field(int v)
{
	return v > FORMAT_FIELD_MAX ? FORMAT_FIELD_MAX : v;
}

/* 1 つの変換指定（%...）の解析結果 */
struct spec {
	bool left;     /* '-' 左寄せ */
	bool zero;     /* '0' ゼロ埋め */
	int width;     /* 最小幅。指定なしは 0 */
	int precision; /* 精度。指定なしは -1 */
};

/* 文字列 s（長さ len）を、幅と寄せ方向に従って出力する */
static void put_padded(struct out *o, const struct spec *sp, const char *s, int len)
{
	int pad = sp->width > len ? sp->width - len : 0;

	if (!sp->left)
		put_repeat(o, ' ', pad);
	for (int i = 0; i < len; i++)
		put(o, s[i]);
	if (sp->left)
		put_repeat(o, ' ', pad);
}

/*
 * 符号なし整数 mag を base 進で出力する。negative なら '-' を付ける。
 * prefix（"0x" など）は数字の前に付く。ゼロ埋めは符号・prefix と数字の間に入る。
 */
static void put_number(struct out *o, const struct spec *sp, unsigned long long mag, bool negative,
		       unsigned base, bool upper, const char *prefix, int min_digits)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char buf[64]; /* 64 ビット値の 2 進表記でも収まる大きさ */
	int n = 0;

	do {
		buf[n++] = digits[mag % base];
		mag /= base;
	} while (mag);
	while (n < min_digits && n < (int)sizeof(buf))
		buf[n++] = '0';

	int prefix_len = 0;
	while (prefix && prefix[prefix_len])
		prefix_len++;

	int len = n + prefix_len + (negative ? 1 : 0);
	int pad = sp->width > len ? sp->width - len : 0;
	/* '-' が指定されたときはゼロ埋めは無視する（C 標準と同じ） */
	bool zero_pad = sp->zero && !sp->left;

	if (!sp->left && !zero_pad)
		put_repeat(o, ' ', pad);
	if (negative)
		put(o, '-');
	for (int i = 0; i < prefix_len; i++)
		put(o, prefix[i]);
	if (zero_pad)
		put_repeat(o, '0', pad);
	while (n)
		put(o, buf[--n]);
	if (sp->left)
		put_repeat(o, ' ', pad);
}

/* 長さ修飾子 */
enum length {
	LEN_NONE,
	LEN_HH,
	LEN_H,
	LEN_L,
	LEN_LL,
	LEN_Z
};

static long long arg_signed(va_list *ap, enum length len)
{
	switch (len) {
	case LEN_HH:
		return (signed char)va_arg(*ap, int);
	case LEN_H:
		return (short)va_arg(*ap, int);
	case LEN_L:
		return va_arg(*ap, long);
	case LEN_LL:
		return va_arg(*ap, long long);
	case LEN_Z:
		/* %zd は size_t に対応する符号付き型。x86_64 では ptrdiff_t と同じ大きさ */
		return va_arg(*ap, ptrdiff_t);
	default:
		return va_arg(*ap, int);
	}
}

static unsigned long long arg_unsigned(va_list *ap, enum length len)
{
	switch (len) {
	case LEN_HH:
		return (unsigned char)va_arg(*ap, unsigned int);
	case LEN_H:
		return (unsigned short)va_arg(*ap, unsigned int);
	case LEN_L:
		return va_arg(*ap, unsigned long);
	case LEN_LL:
		return va_arg(*ap, unsigned long long);
	case LEN_Z:
		return va_arg(*ap, size_t);
	default:
		return va_arg(*ap, unsigned int);
	}
}

int format_v(format_sink sink, void *ctx, const char *fmt, va_list ap_in)
{
	struct out o = {sink, ctx, 0};
	va_list ap;

	/* va_list をポインタで渡して使うため、手元にコピーを作る */
	va_copy(ap, ap_in);

	for (const char *p = fmt; *p; p++) {
		if (*p != '%') {
			put(&o, *p);
			continue;
		}

		const char *start = p; /* 解釈できない指定はそのまま出力するために覚えておく */
		struct spec sp = {false, false, 0, -1};

		p++;
		/* フラグ */
		for (;; p++) {
			if (*p == '-')
				sp.left = true;
			else if (*p == '0')
				sp.zero = true;
			else
				break;
		}
		/* 幅 */
		if (*p == '*') {
			int w = va_arg(ap, int);

			/* 負の幅は '-' フラグ付きの正の幅として扱う（C 標準と同じ） */
			if (w < 0) {
				sp.left = true;
				w = w == INT_MIN ? FORMAT_FIELD_MAX : -w;
			}
			/* 数字で書いた幅と同じ上限を設ける（出力文字数の int があふれないように） */
			sp.width = w > FORMAT_FIELD_MAX ? FORMAT_FIELD_MAX : w;
			p++;
		} else {
			while (*p >= '0' && *p <= '9') {
				sp.width = clamp_field(sp.width * 10 + (*p - '0'));
				p++;
			}
		}
		/* 精度 */
		if (*p == '.') {
			p++;
			sp.precision = 0;
			if (*p == '*') {
				int pr = va_arg(ap, int);

				/* 負の精度は「指定なし」と同じ */
				sp.precision = pr < 0 ? -1 : pr;
				p++;
			} else {
				while (*p >= '0' && *p <= '9') {
					sp.precision = clamp_field(sp.precision * 10 + (*p - '0'));
					p++;
				}
			}
		}
		/* 長さ修飾子 */
		enum length len = LEN_NONE;
		if (*p == 'h') {
			p++;
			len = LEN_H;
			if (*p == 'h') {
				p++;
				len = LEN_HH;
			}
		} else if (*p == 'l') {
			p++;
			len = LEN_L;
			if (*p == 'l') {
				p++;
				len = LEN_LL;
			}
		} else if (*p == 'z') {
			p++;
			len = LEN_Z;
		}

		switch (*p) {
		case '%':
			put(&o, '%');
			break;
		case 'c': {
			char c = (char)va_arg(ap, int);

			put_padded(&o, &sp, &c, 1);
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			int n = 0;

			if (!s)
				s = "(null)";
			/* 精度があれば、それより先は読まない（終端がない配列も渡せる） */
			while ((sp.precision < 0 || n < sp.precision) && s[n])
				n++;
			put_padded(&o, &sp, s, n);
			break;
		}
		case 'd':
		case 'i': {
			long long v = arg_signed(&ap, len);
			/*
			 * 絶対値を求める。v が最小値（例: INT64_MIN）のとき -v は
			 * あふれて未定義動作になるので、-(v + 1) + 1 として符号なしで計算する。
			 */
			unsigned long long mag =
				v < 0 ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;

			put_number(&o, &sp, mag, v < 0, 10, false, NULL, 1);
			break;
		}
		case 'u':
			put_number(&o, &sp, arg_unsigned(&ap, len), false, 10, false, NULL, 1);
			break;
		case 'x':
		case 'X':
			put_number(&o, &sp, arg_unsigned(&ap, len), false, 16, *p == 'X', NULL, 1);
			break;
		case 'p': {
			uintptr_t v = (uintptr_t)va_arg(ap, void *);
			struct spec psp = sp;

			psp.zero = false; /* %p は常に 16 桁で、幅の余りは空白で埋める */
			put_number(&o, &psp, v, false, 16, false, "0x", 16);
			break;
		}
		default:
			/* 解釈できない指定は、そのまま出力する */
			for (const char *q = start; q <= p && *q; q++)
				put(&o, *q);
			if (*p == '\0')
				p--; /* 文字列の終端で止まったときは for の p++ で終端を越えないようにする */
			break;
		}
	}

	va_end(ap);
	return o.count;
}

/* ksnprintf 用の出力先 */
struct buf_sink {
	char *buf;
	size_t size;
	size_t pos;
};

static void buf_put(char c, void *ctx)
{
	struct buf_sink *b = ctx;

	/* 最後の 1 バイトは終端の '\0' のために空けておく */
	if (b->size && b->pos < b->size - 1)
		b->buf[b->pos] = c;
	b->pos++;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct buf_sink b = {buf, size, 0};
	int n = format_v(buf_put, &b, fmt, ap);

	if (size)
		buf[b.pos < size ? b.pos : size - 1] = '\0';
	return n;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	int n = kvsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}
