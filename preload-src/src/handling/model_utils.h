/* model_utils.h - Model validation and cleanup utilities
 *
 * Copyright (C) 2024  Preload Contributors
 *
 * This file is part of preload.
 */

#ifndef MODEL_UTILS_H
#define MODEL_UTILS_H

#include <glib.h>

/*
 * preload_validate_exe - Check if an executable still exists on disk
 *
 * Verifies the file exists and is executable. Also checks inode/mtime
 * to detect if the file was replaced (e.g., after recompile).
 *
 * @path: Absolute path to executable
 * @last_inode: Previously recorded inode (or 0 if unknown)
 * @last_mtime: Previously recorded mtime (or 0 if unknown)
 *
 * Returns:
 *   0  - File exists and unchanged
 *   1  - File was replaced (different inode or mtime)
 *  -1  - File no longer exists
 */
int preload_validate_exe(const char *path, ino_t last_inode, time_t last_mtime);

/*
 * preload_validate_map - Check if a mapped file still exists
 *
 * @path: Absolute path to mapped file
 *
 * Returns: 1 if valid/exists, 0 if deleted or inaccessible
 */
int preload_validate_map(const char *path);

/*
 * preload_cleanup_invalid_entries - Remove stale entries from model
 *
 * Scans all known executables and maps, removing those that no longer
 * exist on disk. This should be called periodically (e.g., during
 * state save) to keep the model accurate.
 *
 * Edge cases handled:
 * - Files temporarily inaccessible (permission, mount)
 * - Prelink renamed files (/bin/bash.#prelink#.XXXX)
 * - NFS stale file handles
 *
 * @exes: Hash table of preload_exe_t, keyed by path
 * @maps: Hash table of preload_map_t
 *
 * Returns: Number of entries removed
 */
int preload_cleanup_invalid_entries(GHashTable *exes, GHashTable *maps);

#endif /* MODEL_UTILS_H */
