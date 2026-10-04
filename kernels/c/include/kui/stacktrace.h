/*
 * スタックトレース（呼び出し履歴）の表示。
 *
 * -fno-omit-frame-pointer でビルドしているので、各関数の先頭で
 * rbp が前の rbp を指す連結リストになっている。これを辿って戻りアドレスを集め、
 * カーネルに埋め込んだシンボル表で関数名に変換して表示する。
 */
#ifndef KUI_STACKTRACE_H
#define KUI_STACKTRACE_H

#include <stdint.h>

/*
 * 表示形式（1 行 1 フレーム）:
 *   "    #<n> 0x<addr> <symbol>+0x<offset>"   シンボルが見つかった場合
 *   "    #<n> 0x<addr> ?"                     見つからない場合
 * rip を #0 とし、rbp から辿った戻りアドレスを続ける。最大 32 フレーム。
 * 不正な rbp（カーネルの範囲外、アラインメント違反）を見つけたら止める。
 */
void stacktrace_print(uint64_t rip, uint64_t rbp);

/* 今の呼び出し位置からのスタックトレースを表示する */
void stacktrace_print_current(void);

/*
 * addr を含む関数の名前を返し、*offset に関数先頭からのずれを入れる。
 * 見つからなければ NULL。
 */
const char *symbol_lookup(uint64_t addr, uint64_t *offset);

#endif
