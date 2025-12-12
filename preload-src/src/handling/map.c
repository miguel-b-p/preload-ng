/* map.c - Memory map management for preload
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

#include "common.h"
#include "map.h"
#include "state.h"

/* Access to global state */
extern preload_state_t state[1];


/* Internal registration functions */
static void
preload_state_register_map (preload_map_t *map)
{
  g_return_if_fail (!g_hash_table_lookup (state->maps, map));

  map->seq = ++(state->map_seq);
  g_hash_table_insert (state->maps, map, GINT_TO_POINTER (1));
  g_ptr_array_add (state->maps_arr, map);
}


static void
preload_state_unregister_map (preload_map_t *map)
{
  g_return_if_fail (g_hash_table_lookup (state->maps, map));

  g_ptr_array_remove (state->maps_arr, map);
  g_hash_table_remove (state->maps, map);
}


preload_map_t *
preload_map_new (const char *path, size_t offset, size_t length)
{
  preload_map_t *map;

  g_return_val_if_fail (path, NULL);

  map = g_malloc0 (sizeof (*map));
  map->path = g_strdup (path);
  map->offset = offset;
  map->length = length;
  map->refcount = 0;
  map->update_time = state->time;
  map->block = -1;
  return map;
}


void
preload_map_free (preload_map_t *map)
{
  g_return_if_fail (map);
  g_return_if_fail (map->refcount == 0);
  g_return_if_fail (map->path);

  g_free (map->path);
  map->path = NULL;
  g_free (map);
}


void
preload_map_ref (preload_map_t *map)
{
  if (map == NULL) return;
  if (!map->refcount)
    preload_state_register_map (map);
  map->refcount++;
}


void
preload_map_unref (preload_map_t *map)
{
  g_return_if_fail (map);
  g_return_if_fail (map->refcount > 0);

  map->refcount--;
  if (!map->refcount) {
    preload_state_unregister_map (map);
    preload_map_free (map);
  }
}


size_t
preload_map_get_size (preload_map_t *map)
{
  g_return_val_if_fail (map, 0);

  return map->length;
}


guint
preload_map_hash (preload_map_t *map)
{
  g_return_val_if_fail (map, 0);
  g_return_val_if_fail (map->path, 0);

  return g_str_hash (map->path)
       + g_direct_hash (GSIZE_TO_POINTER (map->offset))
       + g_direct_hash (GSIZE_TO_POINTER (map->length));
}


gboolean
preload_map_equal (preload_map_t *a, preload_map_t *b)
{
  if (a == b)
    return TRUE;
  if (!a || !b)
    return FALSE;

  if (a->offset != b->offset || a->length != b->length)
    return FALSE;

  if (a->path == b->path)
    return TRUE;

  if (!a->path || !b->path)
    return FALSE;

  return !strcmp (a->path, b->path);
}
