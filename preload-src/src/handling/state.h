/* state.h - Preload state declarations (orchestrator)
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#ifndef STATE_H
#define STATE_H

#include <glib.h>
#include "proc.h"
#include "map.h"
#include "exe.h"
#include "markov.h"

/* preload_state_t: persistent state (the model) */
typedef struct _preload_state_t
{
  /* total seconds that preload have been running,
   * from the beginning of the persistent state. */
  int time;

  /* maps applications known by preload, indexed by
   * exe name, to a preload_exe_t structure. */
  GHashTable *exes;

  /* set of applications that preload is not interested
   * in. typically it is the case that these applications
   * are too small to be a candidate for preloading.
   * mapped value is the size of the binary (sum of the
   * length of the maps. */
  GHashTable *bad_exes;

  /* set of maps used by known executables, indexed by
   * preload_map_t structures. */
  GHashTable *maps;


  /* runtime: */

  GSList *running_exes; /* set of exe structs currently running. */
  GPtrArray *maps_arr; /* set of maps again, in a sortable array. */

  int map_seq; /* increasing sequence of unique numbers to assign to maps. */
  int exe_seq; /* increasing sequence of unique numbers to assign to exes. */

  int last_running_timestamp; /* last time we checked for processes running. */
  int last_accounting_timestamp; /* last time we did accounting on running times, etc. */

  gboolean dirty; /* whether new scan has been performed since last save */
  gboolean model_dirty; /* whether new scan has been performed but no model update yet */

  preload_memory_t memstat; /* system memory stats. */
  int memstat_timestamp; /* last time we updated memory stats. */

} preload_state_t;



/* Global state singleton */
extern preload_state_t state[1];

/* State lifecycle */
void preload_state_load (const char *statefile);
void preload_state_save (const char *statefile);
void preload_state_dump_log (void);
void preload_state_run (const char *statefile);
void preload_state_free (void);


#endif /* STATE_H */
