/* test_markov.c - Unit tests for Markov chain algorithm
 *
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib.h>

#include "state.h"
#include "exe.h"
#include "markov.h"

/* Test macros (duplicated for standalone compilation) */
#define TEST_PASS 0
#define TEST_FAIL 1

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s is NULL\n", __FILE__, __LINE__, #ptr); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s != %s (%d != %d)\n", __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_DOUBLE_EQ(a,b,eps) do { \
    if (fabs((a)-(b)) > (eps)) { \
        fprintf(stderr, "  FAIL: %s:%d: |%g - %g| > %g\n", __FILE__, __LINE__, (double)(a), (double)(b), (double)(eps)); \
        return TEST_FAIL; \
    } \
} while(0)


/* Initialize minimal state for tests */
static void test_init_state(void)
{
    memset(state, 0, sizeof(*state));
    state->time = 100;
    state->last_running_timestamp = 90;
    state->exes = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
    state->bad_exes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    state->maps = g_hash_table_new(NULL, NULL);
    state->maps_arr = g_ptr_array_new();
}

static void test_cleanup_state(void)
{
    if (state->exes) g_hash_table_destroy(state->exes);
    if (state->bad_exes) g_hash_table_destroy(state->bad_exes);
    if (state->maps) g_hash_table_destroy(state->maps);
    if (state->maps_arr) g_ptr_array_free(state->maps_arr, TRUE);
    memset(state, 0, sizeof(*state));
}


static int test_markov_new_initialization(void)
{
    test_init_state();
    
    /* Create two executables */
    preload_exe_t *exe_a = preload_exe_new("/usr/bin/test_a", FALSE, NULL);
    preload_exe_t *exe_b = preload_exe_new("/usr/bin/test_b", FALSE, NULL);
    
    ASSERT_NOT_NULL(exe_a);
    ASSERT_NOT_NULL(exe_b);
    
    /* Register them */
    preload_state_register_exe(exe_a, FALSE);
    preload_state_register_exe(exe_b, FALSE);
    
    /* Create markov chain */
    preload_markov_t *markov = preload_markov_new(exe_a, exe_b, TRUE);
    
    ASSERT_NOT_NULL(markov);
    ASSERT_TRUE(markov->a == exe_a);
    ASSERT_TRUE(markov->b == exe_b);
    ASSERT_EQ(markov->time, 0);
    
    /* State should be 0 (neither running) */
    ASSERT_EQ(markov->state, 0);
    
    /* Cleanup */
    preload_markov_free(markov, NULL);
    preload_exe_free(exe_a);
    preload_exe_free(exe_b);
    test_cleanup_state();
    
    return TEST_PASS;
}


static int test_markov_compute_state(void)
{
    test_init_state();
    
    /* Create two executables */
    preload_exe_t *exe_a = preload_exe_new("/usr/bin/test_a", FALSE, NULL);
    preload_exe_t *exe_b = preload_exe_new("/usr/bin/test_b", FALSE, NULL);
    
    preload_state_register_exe(exe_a, FALSE);
    preload_state_register_exe(exe_b, FALSE);
    
    preload_markov_t *markov = preload_markov_new(exe_a, exe_b, TRUE);
    
    /* State 0: neither running */
    exe_a->running_timestamp = -1;
    exe_b->running_timestamp = -1;
    ASSERT_EQ(markov_compute_state(markov), 0);
    
    /* State 1: only A running */
    exe_a->running_timestamp = state->last_running_timestamp;
    exe_b->running_timestamp = -1;
    ASSERT_EQ(markov_compute_state(markov), 1);
    
    /* State 2: only B running */
    exe_a->running_timestamp = -1;
    exe_b->running_timestamp = state->last_running_timestamp;
    ASSERT_EQ(markov_compute_state(markov), 2);
    
    /* State 3: both running */
    exe_a->running_timestamp = state->last_running_timestamp;
    exe_b->running_timestamp = state->last_running_timestamp;
    ASSERT_EQ(markov_compute_state(markov), 3);
    
    /* Cleanup */
    preload_markov_free(markov, NULL);
    preload_exe_free(exe_a);
    preload_exe_free(exe_b);
    test_cleanup_state();
    
    return TEST_PASS;
}


static int test_markov_correlation_zero(void)
{
    test_init_state();
    state->time = 1000;
    
    preload_exe_t *exe_a = preload_exe_new("/usr/bin/test_a", FALSE, NULL);
    preload_exe_t *exe_b = preload_exe_new("/usr/bin/test_b", FALSE, NULL);
    
    preload_state_register_exe(exe_a, FALSE);
    preload_state_register_exe(exe_b, FALSE);
    
    preload_markov_t *markov = preload_markov_new(exe_a, exe_b, TRUE);
    
    /* Set up: A never runs, B runs all time -> correlation should be 0 */
    exe_a->time = 0;
    exe_b->time = 1000;
    markov->time = 0;
    
    double corr = preload_markov_correlation(markov);
    ASSERT_DOUBLE_EQ(corr, 0.0, 1e-9);
    
    /* Cleanup */
    preload_markov_free(markov, NULL);
    preload_exe_free(exe_a);
    preload_exe_free(exe_b);
    test_cleanup_state();
    
    return TEST_PASS;
}


int test_markov_run(void)
{
    int failed = 0;
    
    fprintf(stderr, "  Running test_markov_new_initialization... ");
    if (test_markov_new_initialization() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_markov_compute_state... ");
    if (test_markov_compute_state() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_markov_correlation_zero... ");
    if (test_markov_correlation_zero() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    return failed;
}
