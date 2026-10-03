/* src/lib/string.c のテスト（ホストでは host_memcpy などの名前でビルドされる） */
#include <kui/string.h>

#include "test.h"

TEST(memcpy_copies_bytes)
{
	char dst[8] = "xxxxxxx";
	void *ret = host_memcpy(dst, "abc", 3);

	EXPECT(ret == dst);
	EXPECT_MEMEQ(dst, "abcxxxx", 8);
}

TEST(memcpy_zero_length_does_nothing)
{
	char dst[4] = "xyz";

	host_memcpy(dst, "abc", 0);
	EXPECT_STREQ(dst, "xyz");
}

TEST(memmove_overlap_forward)
{
	/* コピー先がコピー元より後ろで重なる場合（末尾からコピーする必要がある） */
	char buf[16] = "abcdefgh";

	host_memmove(buf + 2, buf, 6);
	EXPECT_MEMEQ(buf, "ababcdef", 8);
}

TEST(memmove_overlap_backward)
{
	/* コピー先がコピー元より前で重なる場合 */
	char buf[16] = "abcdefgh";

	host_memmove(buf, buf + 2, 6);
	EXPECT_MEMEQ(buf, "cdefghgh", 8);
}

TEST(memmove_same_pointer_and_zero_length)
{
	char buf[8] = "abcdefg";

	EXPECT(host_memmove(buf, buf, 7) == buf);
	EXPECT(host_memmove(buf + 1, buf, 0) == buf + 1);
	EXPECT_STREQ(buf, "abcdefg");
}

TEST(memmove_no_overlap)
{
	char src[8] = "1234567";
	char dst[8] = {0};

	host_memmove(dst, src, 8);
	EXPECT_STREQ(dst, "1234567");
}

TEST(memset_fills_and_truncates_value)
{
	unsigned char buf[6] = {0};
	void *ret = host_memset(buf + 1, 0x1ab, 4); /* int の下位 8 ビット 0xab だけが使われる */

	EXPECT(ret == buf + 1);
	EXPECT_EQ_U(buf[0], 0);
	EXPECT_EQ_U(buf[1], 0xab);
	EXPECT_EQ_U(buf[4], 0xab);
	EXPECT_EQ_U(buf[5], 0);
}

TEST(memcmp_orders_as_unsigned)
{
	EXPECT_EQ(host_memcmp("abc", "abc", 3), 0);
	EXPECT(host_memcmp("abc", "abd", 3) < 0);
	EXPECT(host_memcmp("abd", "abc", 3) > 0);
	EXPECT_EQ(host_memcmp("abc", "xyz", 0), 0);
	/* 0x80 以上のバイトは unsigned char として比べるので、0x01 より大きい */
	EXPECT(host_memcmp("\x80", "\x01", 1) > 0);
}

TEST(strlen_and_strnlen)
{
	EXPECT_EQ_U(host_strlen(""), 0);
	EXPECT_EQ_U(host_strlen("hello"), 5);
	EXPECT_EQ_U(host_strnlen("hello", 3), 3);
	EXPECT_EQ_U(host_strnlen("hello", 10), 5);
	EXPECT_EQ_U(host_strnlen("hello", 0), 0);

	/* strnlen は max より先を読まない（終端のない配列でも安全） */
	char no_nul[3] = {'a', 'b', 'c'};
	EXPECT_EQ_U(host_strnlen(no_nul, 3), 3);
}

TEST(strcmp_orders_strings)
{
	EXPECT_EQ(host_strcmp("", ""), 0);
	EXPECT_EQ(host_strcmp("abc", "abc"), 0);
	EXPECT(host_strcmp("abc", "abd") < 0);
	EXPECT(host_strcmp("abd", "abc") > 0);
	EXPECT(host_strcmp("ab", "abc") < 0);
	EXPECT(host_strcmp("abc", "ab") > 0);
	EXPECT(host_strcmp("\xff", "a") > 0);
}

TEST(strncmp_stops_at_n_and_nul)
{
	EXPECT_EQ(host_strncmp("abcdef", "abcxyz", 3), 0);
	EXPECT(host_strncmp("abcdef", "abcxyz", 4) < 0);
	EXPECT_EQ(host_strncmp("abc", "abc", 100), 0);
	EXPECT(host_strncmp("ab", "abc", 3) < 0);
	EXPECT_EQ(host_strncmp("x", "y", 0), 0);
}

TEST(strlcpy_copies_and_terminates)
{
	char buf[8];

	EXPECT_EQ_U(host_strlcpy(buf, "hello", sizeof(buf)), 5);
	EXPECT_STREQ(buf, "hello");
}

TEST(strlcpy_truncates)
{
	char buf[4] = "zzz";

	/* 戻り値は元の長さなので、切り詰めが起きたことを検出できる */
	EXPECT_EQ_U(host_strlcpy(buf, "hello", sizeof(buf)), 5);
	EXPECT_STREQ(buf, "hel");
}

TEST(strlcpy_size_zero_writes_nothing)
{
	char buf[4] = "zzz";

	EXPECT_EQ_U(host_strlcpy(buf, "hello", 0), 5);
	EXPECT_STREQ(buf, "zzz");
}

TEST(strlcpy_size_one_gives_empty)
{
	char buf[4] = "zzz";

	host_strlcpy(buf, "hello", 1);
	EXPECT_STREQ(buf, "");
}
