/*
 * printf 形式の書式化エンジン。
 *
 * 出力先に依存しないよう、1 文字ずつ「sink」関数へ渡す形にしている。
 * ハードウェアに依存しないので、ホスト上の単体テストでも検証できる。
 *
 * 対応する書式:
 *   変換:   %c %s %d %i %u %x %X %p %%
 *   フラグ: '-'（左寄せ） '0'（ゼロ埋め）
 *   幅:     数字、または '*'
 *   精度:   '.' 数字（%s の最大文字数のみ）
 *   長さ:   hh h l ll z
 */
#ifndef KUI_FORMAT_H
#define KUI_FORMAT_H

#include <stdarg.h>
#include <stddef.h>

typedef void (*format_sink)(char c, void *ctx);

/* 書式化して sink へ渡す。渡した文字数を返す */
int format_v(format_sink sink, void *ctx, const char *fmt, va_list ap);

/*
 * buf へ書式化する（snprintf 相当）。常に '\0' で終端する（size が 0 の場合を除く）。
 * 戻り値は、切り詰めがなかった場合に書かれるはずだった文字数。
 */
int ksnprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif
