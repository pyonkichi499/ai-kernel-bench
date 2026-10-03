/* src/lib/cmdline.c のテスト */
#include <kui/cmdline.h>

#include "test.h"

TEST(cmdline_has_finds_flags_and_keys)
{
	const char *c = "quiet selftest=ubsan  loglevel=debug";

	EXPECT(cmdline_has(c, "quiet"));
	EXPECT(cmdline_has(c, "selftest"));
	EXPECT(cmdline_has(c, "loglevel"));
	EXPECT(!cmdline_has(c, "ubsan")); /* 値の部分には一致しない */
	EXPECT(!cmdline_has(c, "debug"));
}

TEST(cmdline_has_requires_whole_key)
{
	/* "foo" は "foobar" や "foo2=1" には一致しない */
	EXPECT(!cmdline_has("foobar", "foo"));
	EXPECT(!cmdline_has("foo2=1", "foo"));
	EXPECT(!cmdline_has("xfoo", "foo"));
	EXPECT(cmdline_has("foobar foo", "foo"));
	EXPECT(!cmdline_has("foo", "foobar"));
}

TEST(cmdline_empty_and_null)
{
	EXPECT(!cmdline_has("", "quiet"));
	EXPECT(!cmdline_has("   \t ", "quiet"));
	EXPECT(!cmdline_has(NULL, "quiet"));
	EXPECT(!cmdline_has("quiet", ""));

	char out[8] = "zzz";
	EXPECT(!cmdline_get(NULL, "a", out, sizeof(out)));
	EXPECT_STREQ(out, "");
}

TEST(cmdline_tabs_and_edges)
{
	EXPECT(cmdline_has("\tquiet\t", "quiet"));
	EXPECT(cmdline_has("a b c", "c"));
	EXPECT(cmdline_has("a b c", "a"));
}

TEST(cmdline_get_returns_value)
{
	char out[16];

	EXPECT(cmdline_get("quiet selftest=ubsan x=1", "selftest", out, sizeof(out)));
	EXPECT_STREQ(out, "ubsan");
	EXPECT(cmdline_get("quiet selftest=ubsan x=1", "x", out, sizeof(out)));
	EXPECT_STREQ(out, "1");
}

TEST(cmdline_get_empty_value)
{
	char out[8] = "zzz";

	EXPECT(cmdline_get("key= other", "key", out, sizeof(out)));
	EXPECT_STREQ(out, "");
}

TEST(cmdline_get_missing_or_flag_only)
{
	char out[8] = "zzz";

	EXPECT(!cmdline_get("quiet", "quiet", out, sizeof(out)));
	EXPECT_STREQ(out, "");
	EXPECT(!cmdline_get("a=1", "b", out, sizeof(out)));
	EXPECT_STREQ(out, "");
}

TEST(cmdline_get_first_occurrence_wins)
{
	char out[8];

	EXPECT(cmdline_get("a=1 a=2", "a", out, sizeof(out)));
	EXPECT_STREQ(out, "1");
	/* 最初の項目が値なしなら、後ろに値付きがあっても false */
	EXPECT(!cmdline_get("a a=2", "a", out, sizeof(out)));
}

TEST(cmdline_get_value_may_contain_equals)
{
	char out[16];

	EXPECT(cmdline_get("root=a=b", "root", out, sizeof(out)));
	EXPECT_STREQ(out, "a=b");
}

TEST(cmdline_get_truncates)
{
	char out[4];

	EXPECT(cmdline_get("k=abcdef", "k", out, sizeof(out)));
	EXPECT_STREQ(out, "abc");
}

TEST(cmdline_get_out_size_zero)
{
	char out[1] = {'z'};

	/* out_size が 0 なら out には何も書かない */
	EXPECT(cmdline_get("k=v", "k", out, 0));
	EXPECT_EQ(out[0], 'z');
}

/* key に '=' や空白が入っていたら、どの項目にも一致しない */
TEST(cmdline_key_with_separator_never_matches)
{
	char out[8];

	EXPECT(!cmdline_has("a=b c", "a=b"));
	EXPECT(!cmdline_has("a b", "a b"));
	EXPECT(!cmdline_get("a=b=c", "a=b", out, sizeof(out)));
	EXPECT_STREQ(out, "");
}

/* 連続した空白やタブ、先頭・末尾の空白があっても項目を正しく区切る */
TEST(cmdline_mixed_whitespace)
{
	char out[8];
	const char *cl = " \t a=1 \t\t b \t c=xyz\t ";

	EXPECT(cmdline_has(cl, "a"));
	EXPECT(cmdline_has(cl, "b"));
	EXPECT(cmdline_get(cl, "c", out, sizeof(out)));
	EXPECT_STREQ(out, "xyz");
	EXPECT(!cmdline_has(cl, "d"));
}

/* out_size が 1 なら値を持てないが、key が見つかれば true で out は空文字列 */
TEST(cmdline_get_out_size_one)
{
	char out[1] = {'z'};

	EXPECT(cmdline_get("k=v", "k", out, 1));
	EXPECT_EQ(out[0], '\0');
}
