/* test_state_io.c - Unit tests for state I/O persistence
 *
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>

#include "state.h"
#include "state_io.h"
#include "map.h"
#include "exe.h"

/* Test macros */
#define TEST_PASS 0
#define TEST_FAIL 1

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return TEST_FAIL; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s is not NULL\n", __FILE__, __LINE__, #ptr); \
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


/* Initialize minimal state for tests */
static void test_init_state(void)
{
    memset(state, 0, sizeof(*state));
    state->time = 100;
    state->last_running_timestamp = 90;
    state->exes = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)preload_exe_free);
    state->bad_exes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    state->maps = g_hash_table_new_full((GHashFunc)preload_map_hash, (GEqualFunc)preload_map_equal, NULL, NULL);
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


static int test_state_io_write_empty(void)
{
    test_init_state();
    
    char tmpfile[] = "/tmp/preload_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT_TRUE(fd >= 0);
    close(fd);
    
    /* Write empty state */
    state->dirty = TRUE;
    char *errmsg = preload_state_write_file(tmpfile);
    ASSERT_NULL(errmsg);
    
    /* File should exist */
    ASSERT_TRUE(g_file_test(tmpfile, G_FILE_TEST_EXISTS));
    
    /* Cleanup */
    unlink(tmpfile);
    test_cleanup_state();
    
    return TEST_PASS;
}


static int test_state_io_roundtrip(void)
{
    test_init_state();
    
    char tmpfile[] = "/tmp/preload_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT_TRUE(fd >= 0);
    close(fd);
    
    /* Create some test data */
    state->time = 500;
    
    /* Add an executable with a map */
    preload_map_t *map = preload_map_new("/usr/lib/libc.so.6", 0, 4096);
    preload_map_ref(map);
    
    GPtrArray *exemaps = g_ptr_array_new();
    preload_exemap_t *exemap = preload_exemap_new(map);
    g_ptr_array_add(exemaps, exemap);
    
    preload_exe_t *exe = preload_exe_new("/usr/bin/bash", FALSE, exemaps);
    exe->time = 100;
    exe->update_time = 50;
    preload_state_register_exe(exe, FALSE);
    
    int original_time = state->time;
    int original_exe_count = g_hash_table_size(state->exes);
    int original_map_count = g_hash_table_size(state->maps);
    
    /* Write state */
    state->dirty = TRUE;
    char *errmsg = preload_state_write_file(tmpfile);
    ASSERT_NULL(errmsg);
    
    /* Reset state */
    test_cleanup_state();
    test_init_state();
    
    /* Read state back */
    errmsg = preload_state_read_file(tmpfile);
    ASSERT_NULL(errmsg);
    
    /* Verify */
    ASSERT_EQ(state->time, original_time);
    ASSERT_EQ(g_hash_table_size(state->exes), original_exe_count);
    ASSERT_EQ(g_hash_table_size(state->maps), original_map_count);
    
    /* Verify exe was restored correctly */
    preload_exe_t *restored_exe = g_hash_table_lookup(state->exes, "/usr/bin/bash");
    ASSERT_NOT_NULL(restored_exe);
    ASSERT_EQ(restored_exe->time, 100);
    
    /* Cleanup */
    unlink(tmpfile);
    test_cleanup_state();
    
    return TEST_PASS;
}


static int test_state_io_format_compatibility(void)
{
    test_init_state();
    
    char tmpfile[] = "/tmp/preload_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT_TRUE(fd >= 0);
    close(fd);
    
    /* Create test data */
    state->time = 1000;
    
    preload_exe_t *exe1 = preload_exe_new("/usr/bin/firefox", FALSE, NULL);
    exe1->time = 200;
    preload_state_register_exe(exe1, FALSE);
    
    preload_exe_t *exe2 = preload_exe_new("/usr/bin/vim", FALSE, NULL);
    exe2->time = 150;
    preload_state_register_exe(exe2, FALSE);
    
    /* Write state */
    state->dirty = TRUE;
    char *errmsg = preload_state_write_file(tmpfile);
    ASSERT_NULL(errmsg);
    
    /* Read the file and check format */
    gchar *contents = NULL;
    gsize length = 0;
    GError *err = NULL;
    gboolean ok = g_file_get_contents(tmpfile, &contents, &length, &err);
    if (!ok) {
        g_clear_error(&err);
        return TEST_FAIL;
    }
    
    /* Verify format: should start with PRELOAD tag */
    ASSERT_TRUE(g_str_has_prefix(contents, "PRELOAD\t"));
    
    /* Should contain EXE tags */
    ASSERT_TRUE(strstr(contents, "EXE\t") != NULL);
    
    /* Cleanup */
    g_free(contents);
    unlink(tmpfile);
    test_cleanup_state();
    
    return TEST_PASS;
}


int test_state_io_run(void)
{
    int failed = 0;
    
    fprintf(stderr, "  Running test_state_io_write_empty... ");
    if (test_state_io_write_empty() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_state_io_roundtrip... ");
    if (test_state_io_roundtrip() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    fprintf(stderr, "  Running test_state_io_format_compatibility... ");
    if (test_state_io_format_compatibility() == TEST_PASS) {
        fprintf(stderr, "PASS\n");
    } else {
        failed++;
    }
    
    return failed;
}
