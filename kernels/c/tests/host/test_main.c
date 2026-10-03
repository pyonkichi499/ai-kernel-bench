/* ホスト単体テストの実行部（使い方は test.h を参照） */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "test.h"

#define MAX_TESTS 1024

struct test_case {
	const char *name;
	void (*fn)(void);
};

static struct test_case tests[MAX_TESTS];
static int test_count;
static int current_failures; /* 実行中のテストで失敗した確認の数 */

void test_register(const char *name, void (*fn)(void))
{
	if (test_count >= MAX_TESTS) {
		fprintf(stderr, "テストが多すぎる（MAX_TESTS を増やすこと）\n");
		exit(2);
	}
	tests[test_count].name = name;
	tests[test_count].fn = fn;
	test_count++;
}

void test_fail(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	current_failures++;
	printf("    %s:%d: ", file, line);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

int main(void)
{
	int failed = 0;

	printf("host-test: %d 件のテストを実行\n", test_count);
	for (int i = 0; i < test_count; i++) {
		current_failures = 0;
		tests[i].fn();
		if (current_failures) {
			failed++;
			printf("FAIL %s\n", tests[i].name);
		} else {
			printf("PASS %s\n", tests[i].name);
		}
		fflush(stdout);
	}
	printf("host-test: %d passed, %d failed\n", test_count - failed, failed);
	return failed ? 1 : 0;
}
