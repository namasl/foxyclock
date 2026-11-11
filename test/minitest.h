/* Minimal host-side unit test harness. */
#ifndef MINITEST_H
#define MINITEST_H
#include <stdio.h>
#include <string.h>

static int mt_failed_asserts, mt_tests_run, mt_tests_failed, mt_cur_failed;

#define ASSERT(cond) do { if (!(cond)) { \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    mt_failed_asserts++; mt_cur_failed = 1; } } while (0)

#define ASSERT_EQ_INT(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", \
        __FILE__, __LINE__, #a, #b, _a, _b); mt_failed_asserts++; mt_cur_failed = 1; } } while (0)

#define ASSERT_EQ_STR(a, b) do { const char *_a = (a), *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { printf("  FAIL %s:%d: %s == %s (\"%s\" != \"%s\")\n", \
        __FILE__, __LINE__, #a, #b, _a ? _a : "(null)", _b ? _b : "(null)"); \
        mt_failed_asserts++; mt_cur_failed = 1; } } while (0)

#define ASSERT_CONTAINS(hay, needle) do { const char *_h = (hay); \
    if (!_h || !strstr(_h, (needle))) { printf("  FAIL %s:%d: \"%s\" does not contain \"%s\"\n", \
        __FILE__, __LINE__, _h ? _h : "(null)", (needle)); mt_failed_asserts++; mt_cur_failed = 1; } } while (0)

#define RUN(fn) do { mt_cur_failed = 0; mt_tests_run++; fn(); \
    if (mt_cur_failed) { mt_tests_failed++; printf("FAILED %s\n", #fn); } } while (0)

#define TEST_SUMMARY(name) (printf("%s: %d passed, %d failed\n", name, \
    mt_tests_run - mt_tests_failed, mt_tests_failed), mt_tests_failed != 0)
#endif
