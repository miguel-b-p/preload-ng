/* test_vomm.c - Unit tests for VOMM algorithm
 *
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "state.h"
#include "exe.h"
#include "vomm.h"

/* Test macros */
#define TEST_PASS 0
#define TEST_FAIL 1

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s is NULL\n", __FILE__, __LINE__, #ptr); \
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


static int test_vomm_init_cleanup(void)
{
    test_init_state();
    
    /* Initialize VOMM */
    gboolean ok = vomm_init();
    ASSERT_TRUE(ok);
    
    /* Cleanup should not crash */
    vomm_cleanup();
    
    /* Should be safe to call cleanup again */
    vomm_cleanup();
    
    test_cleanup_state();
    return TEST_PASS;
}


static int test_vomm_update_single(void)
{
    test_init_state();
    
    gboolean ok = vomm_init();
    ASSERT_TRUE(ok);
    
    /* Create an executable */
    preload_exe_t *exe = preload_exe_new("/usr/bin/firefox", FALSE, NULL);
    ASSERT_NOT_NULL(exe);
    preload_state_register_exe(exe, FALSE);
    
    /* Update VOMM with the exe - should not crash */
    vomm_update(exe);
    
    /* Update again - should handle duplicates */
    vomm_update(exe);
    
    vomm_cleanup();
    preload_exe_free(exe);
    test_cleanup_state();
    
    return TEST_PASS;
}


static int test_vomm_update_sequence(void)
{
    test_init_state();
    
    gboolean ok = vomm_init();
    ASSERT_TRUE(ok);
    
    /* Create multiple executables */
    preload_exe_t *exe1 = preload_exe_new("/usr/bin/firefox", FALSE, NULL);
    preload_exe_t *exe2 = preload_exe_new("/usr/bin/vim", FALSE, NULL);
    preload_exe_t *exe3 = preload_exe_new("/usr/bin/bash", FALSE, NULL);
    
    preload_state_register_exe(exe1, FALSE);
    preload_state_register_exe(exe2, FALSE);
    preload_state_register_exe(exe3, FALSE);
    
    /* Simulate sequence: firefox -> vim -> bash -> firefox */
    vomm_update(exe1);
    vomm_update(exe2);
    vomm_update(exe3);
    vomm_update(exe1);  /* Back to firefox */
    
    /* Predict should not crash */
    vomm_predict();
    
    vomm_cleanup();
    preload_exe_free(exe1);
    preload_exe_free(exe2);
    preload_exe_free(exe3);
    test_cleanup_state();
    
    return TEST_PASS;
}


static int test_vomm_predict_empty(void)
{
    test_init_state();
    
    gboolean ok = vomm_init();
    ASSERT_TRUE(ok);
    
    /* Predict with no history - should not crash */
    vomm_predict();
    
    vomm_cleanup();
    test_cleanup_state();
    
    return TEST_PASS;
}


static int test_vomm_null_safety(void)
{
    test_init_state();
    
    /* vomm_update with NULL should not crash */
    vomm_update(NULL);
    
    /* vomm_predict without init should not crash */
    vomm_predict();
    
    test_cleanup_state();
    return TEST_PASS;
}


int test_vomm_run(void)
{
    int failed = 0;
    
    fprintf(stderr, "  Running test_vomm_init_cleanup... ");
    if (test_vomm_init_cleanup() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_vomm_update_single... ");
    if (test_vomm_update_single() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_vomm_update_sequence... ");
    if (test_vomm_update_sequence() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_vomm_predict_empty... ");
    if (test_vomm_predict_empty() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_vomm_null_safety... ");
    if (test_vomm_null_safety() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    return failed;
}
