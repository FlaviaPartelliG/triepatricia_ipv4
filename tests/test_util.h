/* test_util.h - minimal assert-based test harness (no external deps). */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>

static int  test_failures = 0;
static int  test_checks   = 0;
static const char *test_name = "?";

#define TEST_BEGIN(name) do { test_name = (name); \
    printf("== %s ==\n", test_name); } while (0)

#define CHECK(cond) do { \
    test_checks++; \
    if (!(cond)) { \
        test_failures++; \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_MSG(cond, ...) do { \
    test_checks++; \
    if (!(cond)) { \
        test_failures++; \
        fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while (0)

#define TEST_REPORT() do { \
    printf("-- %s: %d checks, %d failures --\n", \
           test_name, test_checks, test_failures); \
    return test_failures == 0 ? 0 : 1; \
} while (0)

#endif /* TEST_UTIL_H */
