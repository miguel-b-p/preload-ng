/* state_io.h - State persistence declarations
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#ifndef STATE_IO_H
#define STATE_IO_H

#include <glib.h>

/**
 * preload_state_read_file:
 * @statefile: Path to the state file. Must not be NULL.
 *
 * Reads the preload state from the specified file.
 *
 * Returns: NULL on success. On failure, returns a dynamically allocated
 * error message that must be freed by the caller using g_free().
 *
 * Thread-safety: Not thread-safe.
 */
char * preload_state_read_file (const char *statefile);

/**
 * preload_state_write_file:
 * @statefile: Path to the state file. Must not be NULL.
 *
 * Writes the current preload state to the specified file.
 *
 * Returns: NULL on success. On failure, returns a dynamically allocated
 * error message that must be freed by the caller using g_free().
 *
 * Thread-safety: Not thread-safe.
 */
char * preload_state_write_file (const char *statefile);

#endif /* STATE_IO_H */
