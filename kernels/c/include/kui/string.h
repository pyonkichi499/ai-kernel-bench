/*
 * メモリ操作と文字列操作の基本関数。
 *
 * カーネルには C 標準ライブラリがないので自前で用意する。
 * memcpy / memmove / memset / memcmp はコンパイラが暗黙に呼び出すこともあるため、
 * 標準と同じ名前・同じ仕様で実装する必要がある。
 *
 * ホスト上の単体テストでは、ホストの libc と名前が衝突しないよう
 * KUI_HOST_TEST を定義して host_memcpy などの名前でビルドする。
 */
#ifndef KUI_STRING_H
#define KUI_STRING_H

#include <stddef.h>

#ifdef KUI_HOST_TEST
#define KUI_STRFN(name) host_##name
#else
#define KUI_STRFN(name) name
#endif

void *KUI_STRFN(memcpy)(void *restrict dst, const void *restrict src, size_t n);
void *KUI_STRFN(memmove)(void *dst, const void *src, size_t n);
void *KUI_STRFN(memset)(void *dst, int c, size_t n);
int KUI_STRFN(memcmp)(const void *a, const void *b, size_t n);
size_t KUI_STRFN(strlen)(const char *s);
size_t KUI_STRFN(strnlen)(const char *s, size_t max);
int KUI_STRFN(strcmp)(const char *a, const char *b);
int KUI_STRFN(strncmp)(const char *a, const char *b, size_t n);

/*
 * dst（大きさ size）へ src をコピーし、必ず '\0' で終端する（size が 0 の場合を除く）。
 * 戻り値は src の長さ（切り詰めの検出に使える）。BSD の strlcpy と同じ仕様。
 */
size_t KUI_STRFN(strlcpy)(char *restrict dst, const char *restrict src, size_t size);

#endif
