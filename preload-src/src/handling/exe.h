/* exe.h - Executable and exemap declarations
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#ifndef EXE_H
#define EXE_H

#include <glib.h>
#include "map.h"

/* Forward declarations */
typedef struct _preload_state_t preload_state_t;
typedef struct _preload_markov_t preload_markov_t;

/* preload_exemap_t: structure holding information
 * about a mapped section in an exe. */
typedef struct _preload_exemap_t
{
  preload_map_t *map;
  double prob; /* probability that this map is used when exe is running. */
} preload_exemap_t;


/* preload_exe_t: structure holding information
 * about an executable. */
typedef struct _preload_exe_t
{
  char *path; /* absolute path of the executable. */
  int time; /* total time that this has been running, ever. */
  int update_time; /* last time it was probed. */
  GPtrArray *markovs; /* set of markov chains with other exes. */
  GPtrArray *exemaps; /* set of exemap structures. */

  /* runtime: */
  size_t size; /* sum of the size of the maps, in bytes. */
  int running_timestamp; /* last time it was running. */
  int change_timestamp; /* time started/stopped running. */
  double lnprob; /* log-probability of NOT being needed in next period. */
  int seq; /* unique exe sequence number. */
} preload_exe_t;

/* Check if executable is currently running (implemented in exe.c) */
gboolean exe_is_running(preload_exe_t *exe);


/* Exe functions */
preload_exe_t * preload_exe_new (const char *path, gboolean running, GPtrArray *exemaps);
void preload_exe_free (preload_exe_t *exe);
preload_exemap_t * preload_exe_map_new (preload_exe_t *exe, preload_map_t *map);

/* Exemap functions */
preload_exemap_t * preload_exemap_new (preload_map_t *map);
void preload_exemap_free (gpointer data, gpointer user_data);
void preload_exemap_foreach (GHFunc func, gpointer user_data);

/* Registration */
void preload_state_register_exe (preload_exe_t *exe, gboolean create_markovs);
void preload_state_unregister_exe (preload_exe_t *exe);

#endif /* EXE_H */
