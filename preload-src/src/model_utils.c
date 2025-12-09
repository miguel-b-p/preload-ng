/* model_utils.c - Model validation and cleanup utilities
 *
 * Copyright (C) 2024  Preload Contributors
 *
 * This file is part of preload.
 */

#include "common.h"
#include "model_utils.h"
#include "log.h"
#include "state.h"

#include <sys/stat.h>

int
preload_validate_exe(const char *path, ino_t last_inode, time_t last_mtime)
{
  struct stat st;

  if (!path || *path != '/') {
    return -1;
  }

  if (stat(path, &st) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) {
      /* File doesn't exist */
      return -1;
    }
    /* Other error (permission, etc.) - assume still valid */
    g_debug("stat(%s) failed: %s - assuming valid", path, strerror(errno));
    return 0;
  }

  /* Check if executable */
  if (!S_ISREG(st.st_mode)) {
    return -1;
  }

  /* Check if file was replaced (different inode or newer mtime) */
  if (last_inode != 0 && st.st_ino != last_inode) {
    g_debug("File %s has different inode (was %lu, now %lu) - replaced",
            path, (unsigned long)last_inode, (unsigned long)st.st_ino);
    return 1;
  }

  if (last_mtime != 0 && st.st_mtime > last_mtime) {
    g_debug("File %s has newer mtime - replaced", path);
    return 1;
  }

  return 0;
}

int
preload_validate_map(const char *path)
{
  struct stat st;

  if (!path || *path != '/') {
    return 0;
  }

  /* Special handling for pseudo-filesystems */
  if (strncmp(path, "/proc/", 6) == 0 ||
      strncmp(path, "/sys/", 5) == 0 ||
      strncmp(path, "/dev/", 5) == 0) {
    /* These are always "valid" even if not stat-able */
    return 1;
  }

  if (stat(path, &st) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) {
      return 0;
    }
    /* Other error - assume valid (might be permission issue) */
    return 1;
  }

  return 1;
}

/* 
 * Context for cleanup iteration.
 * We collect items to remove first, then remove them after iteration.
 */
typedef struct {
  GSList *exes_to_remove;
  GSList *maps_to_remove;
  int removed_count;
} cleanup_context_t;

static void
check_exe_validity(gpointer key, preload_exe_t *exe, cleanup_context_t *ctx)
{
  int status;

  /* Skip if exe is currently running - don't invalidate active processes */
  if (exe_is_running(exe)) {
    return;
  }

  /* Validate the executable */
  status = preload_validate_exe(exe->path, 0, 0);
  
  if (status == -1) {
    /* File no longer exists - mark for removal */
    g_debug("Marking deleted exe for removal: %s", exe->path);
    ctx->exes_to_remove = g_slist_prepend(ctx->exes_to_remove, exe);
  } else if (status == 1) {
    /* File was replaced - could optionally reset its stats here */
    g_debug("Exe was replaced: %s", exe->path);
    /* For now, keep it but its maps may be outdated */
  }
}

static void
check_map_validity(preload_map_t *map, gpointer data, cleanup_context_t *ctx)
{
  (void)data;

  if (!preload_validate_map(map->path)) {
    g_debug("Marking deleted map for removal: %s", map->path);
    ctx->maps_to_remove = g_slist_prepend(ctx->maps_to_remove, map);
  }
}

static void
remove_invalid_exe(preload_exe_t *exe, cleanup_context_t *ctx)
{
  /* Guard against exe that was already unregistered or has NULL path */
  if (!exe || !exe->path) {
    g_debug("Skipping invalid exe pointer in cleanup");
    return;
  }
  
  /* Verify exe is still in the hash table before unregistering */
  if (!g_hash_table_lookup(state->exes, exe->path)) {
    g_debug("Exe already removed from hash table: %s", exe->path);
    return;
  }

  g_message("Removing deleted executable from model: %s", exe->path);
  preload_state_unregister_exe(exe);
  preload_exe_free(exe);
  ctx->removed_count++;
}

int
preload_cleanup_invalid_entries(GHashTable *exes, GHashTable *maps)
{
  cleanup_context_t ctx;
  
  ctx.exes_to_remove = NULL;
  ctx.maps_to_remove = NULL;
  ctx.removed_count = 0;

  if (!exes || !maps) {
    return 0;
  }

  /* First pass: identify invalid entries */
  g_hash_table_foreach(exes, (GHFunc)check_exe_validity, &ctx);
  
  /* Note: Maps are reference-counted and cleaned up when their
   * referencing exes are removed. We could also proactively check
   * maps, but it's more efficient to let reference counting handle it. */

  /* Second pass: remove identified entries */
  g_slist_foreach(ctx.exes_to_remove, (GFunc)remove_invalid_exe, &ctx);
  g_slist_free(ctx.exes_to_remove);
  g_slist_free(ctx.maps_to_remove);

  if (ctx.removed_count > 0) {
    g_message("Cleaned up %d stale entries from model", ctx.removed_count);
  }

  return ctx.removed_count;
}
