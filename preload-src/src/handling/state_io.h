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

/* Read state from file, returns NULL on success or error message on failure */
char * preload_state_read_file (const char *statefile);

/* Write state to file, returns NULL on success or error message on failure */
char * preload_state_write_file (const char *statefile);

#endif /* STATE_IO_H */
