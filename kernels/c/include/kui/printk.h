/* カーネルのログ出力 */
#ifndef KUI_PRINTK_H
#define KUI_PRINTK_H

#include <stdarg.h>

/* ログをシリアルへ出力する。書式は kui/format.h を参照 */
int printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int vprintk(const char *fmt, va_list ap);

#endif
