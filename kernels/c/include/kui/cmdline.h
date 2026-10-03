/*
 * カーネルのコマンドラインの解析。
 *
 * コマンドラインは空白区切りの項目の並び。各項目は "key" または "key=value" の形。
 *   例: "selftest=ubsan loglevel=debug quiet"
 */
#ifndef KUI_CMDLINE_H
#define KUI_CMDLINE_H

#include <stdbool.h>
#include <stddef.h>

/* key という項目（"key" でも "key=..." でもよい）があれば true */
bool cmdline_has(const char *cmdline, const char *key);

/*
 * "key=value" の value を out（大きさ out_size）へコピーして true を返す。
 * key がない、または "=" がない場合は false を返し、out は空文字列になる。
 * value が長すぎる場合は切り詰める。
 */
bool cmdline_get(const char *cmdline, const char *key, char *out, size_t out_size);

#endif
