/* map.h - Memory map declarations
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#ifndef MAP_H
#define MAP_H

#include <glib.h>

/* Forward declaration */
typedef struct _preload_state_t preload_state_t;

/* preload_map_t: structure holding information
 * about a mapped section. */
typedef struct _preload_map_t
{
  char *path; /* absolute path of the mapped file. */
  size_t offset; /* in bytes. */
  size_t length; /* in bytes. */
  int update_time; /* last time it was probed. */

  /* runtime: */
  int refcount; /* number of exes linking to this. */
  double lnprob; /* log-probability of NOT being needed in next period. */
  int seq; /* unique map sequence number. */
  int block; /* on-disk location of the start of the map. */
  int priv; /* for private local use of functions. */
} preload_map_t;


/* Functions */

/* duplicates path */
preload_map_t * preload_map_new (const char *path, size_t offset, size_t length);
void preload_map_free (preload_map_t *map);
void preload_map_ref (preload_map_t *map);
void preload_map_unref (preload_map_t *map);
size_t preload_map_get_size (preload_map_t *map);
guint preload_map_hash (preload_map_t *map);
gboolean preload_map_equal (preload_map_t *a, preload_map_t *b);

#endif /* MAP_H */
