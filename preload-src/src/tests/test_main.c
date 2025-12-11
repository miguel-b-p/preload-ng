/* test_main.c - Unit test runner for preload-ng
 *
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

/* Test result macros */
#define TEST_PASS 0
#define TEST_FAIL 1

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s is NULL\n", __FILE__, __LINE__, #ptr); \
        return TEST_FAIL; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    fprintf(stderr, "  Running %s... ", #test_func); \
    if (test_func() == TEST_PASS) { \
        fprintf(stderr, "PASS\n"); \
        passed++; \
    } else { \
        failed++; \
    } \
    total++; \
} while(0)


/* Forward declarations of test suites */
extern int test_markov_run(void);
extern int test_vomm_run(void);
extern int test_state_io_run(void);


int main(int argc, char **argv)
{
    int total = 0;
    int passed = 0;
    int failed = 0;
    
    (void)argc;
    (void)argv;
    
    fprintf(stderr, "\n=== preload-ng Unit Tests ===\n\n");
    
    /* Run test suites */
    fprintf(stderr, "[Markov Tests]\n");
    failed += test_markov_run();
    
    fprintf(stderr, "\n[VOMM Tests]\n");
    failed += test_vomm_run();
    
    fprintf(stderr, "\n[State I/O Tests]\n");
    failed += test_state_io_run();
    
    fprintf(stderr, "\n=== Test Summary ===\n");
    fprintf(stderr, "Failed: %d\n", failed);
    
    return failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
