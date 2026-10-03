/*
 * カーネルのコマンドラインの解析（仕様は kui/cmdline.h を参照）。
 *
 * 項目は空白（スペースまたはタブ）で区切られる。引用符やエスケープには対応しない。
 * 同じ key が複数あるときは、最初の項目だけを見る。
 * kui/string.h に依存しないので、ホスト上の単体テストでそのまま検証できる。
 */
#include <kui/cmdline.h>

static bool is_space(char c)
{
	return c == ' ' || c == '\t';
}

/*
 * cmdline から key に一致する最初の項目を探す。
 * 見つかれば、その項目の key の直後（'='、空白、終端のいずれか）を指すポインタを返す。
 */
static const char *find_item(const char *cmdline, const char *key)
{
	if (!cmdline || !key || !*key)
		return NULL;
	/* key 自体に '=' や空白が入っていると項目の区切りと区別できないので、一致なしとする */
	for (const char *k = key; *k; k++) {
		if (*k == '=' || is_space(*k))
			return NULL;
	}

	const char *p = cmdline;

	for (;;) {
		while (is_space(*p))
			p++;
		if (!*p)
			return NULL;

		/* 項目の key 部分（'=' か空白か終端まで）と key を比べる */
		const char *k = key;
		const char *q = p;

		while (*k && *q == *k) {
			q++;
			k++;
		}
		if (!*k && (*q == '=' || is_space(*q) || !*q))
			return q;

		/* 一致しなかったので、次の項目まで読み飛ばす */
		while (*p && !is_space(*p))
			p++;
	}
}

bool cmdline_has(const char *cmdline, const char *key)
{
	return find_item(cmdline, key) != NULL;
}

bool cmdline_get(const char *cmdline, const char *key, char *out, size_t out_size)
{
	if (out_size)
		out[0] = '\0';

	const char *p = find_item(cmdline, key);

	if (!p || *p != '=')
		return false;
	p++;

	size_t n = 0;

	while (p[n] && !is_space(p[n])) {
		if (out_size && n < out_size - 1)
			out[n] = p[n];
		n++;
	}
	if (out_size)
		out[n < out_size - 1 ? n : out_size - 1] = '\0';
	return true;
}
