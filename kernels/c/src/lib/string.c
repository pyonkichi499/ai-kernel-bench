/*
 * メモリ操作と文字列操作の基本関数（仕様は kui/string.h を参照）。
 *
 * 注意: memset などの実装の中で、コンパイラが「このループは memset と同じだ」と
 * 判断して memset の呼び出しに置き換えると、自分自身を呼ぶ無限再帰になる。
 * これを防ぐため、このファイルは -fno-builtin でビルドし、さらに各関数に
 * no_builtin 属性を付けている。
 */
#include <stdint.h>

#include <kui/string.h>

#define NO_BUILTIN __attribute__((no_builtin))

NO_BUILTIN void *KUI_STRFN(memcpy)(void *restrict dst, const void *restrict src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

NO_BUILTIN void *KUI_STRFN(memmove)(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d == s || n == 0)
		return dst;
	/*
	 * 重なりがあっても正しくコピーするため、コピー先がコピー元より後ろにあるときは
	 * 末尾から先頭に向かってコピーする。別々の配列を指すポインタ同士の大小比較は
	 * C では未定義なので、整数に変換して比較する。
	 */
	if ((uintptr_t)d < (uintptr_t)s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dst;
}

NO_BUILTIN void *KUI_STRFN(memset)(void *dst, int c, size_t n)
{
	unsigned char *d = dst;

	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}

NO_BUILTIN int KUI_STRFN(memcmp)(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;

	for (; n; n--, x++, y++) {
		if (*x != *y)
			return *x < *y ? -1 : 1;
	}
	return 0;
}

NO_BUILTIN size_t KUI_STRFN(strlen)(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

NO_BUILTIN size_t KUI_STRFN(strnlen)(const char *s, size_t max)
{
	size_t n = 0;

	while (n < max && s[n])
		n++;
	return n;
}

NO_BUILTIN int KUI_STRFN(strcmp)(const char *a, const char *b)
{
	/* 比較は unsigned char として行う（C 標準と同じ） */
	const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;

	while (*x && *x == *y) {
		x++;
		y++;
	}
	return *x == *y ? 0 : (*x < *y ? -1 : 1);
}

NO_BUILTIN int KUI_STRFN(strncmp)(const char *a, const char *b, size_t n)
{
	const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;

	for (; n; n--, x++, y++) {
		if (*x != *y)
			return *x < *y ? -1 : 1;
		if (*x == '\0')
			return 0;
	}
	return 0;
}

NO_BUILTIN size_t KUI_STRFN(strlcpy)(char *restrict dst, const char *restrict src, size_t size)
{
	size_t len = KUI_STRFN(strlen)(src);

	if (size) {
		size_t n = len < size - 1 ? len : size - 1;

		KUI_STRFN(memcpy)(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}
