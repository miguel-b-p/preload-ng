/* exe.c - Executable and exemap management for preload
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
#include "exe.h"
#include "map.h"
#include "markov.h"
#include "state.h"


/* Check if executable is currently running */
gboolean
exe_is_running(preload_exe_t *exe)
{
  if (!state)
    return FALSE;
  return exe->running_timestamp >= state->last_running_timestamp;
}


preload_exemap_t *
preload_exemap_new (preload_map_t *map)
{
  preload_exemap_t *exemap;

  g_return_val_if_fail (map, NULL);

  preload_map_ref (map);
  exemap = g_malloc (sizeof (*exemap));
  exemap->map = map;
  exemap->prob = 1.0;
  return exemap;
}


void
preload_exemap_free (gpointer data, gpointer G_GNUC_UNUSED user_data)
{
  preload_exemap_t *exemap = (preload_exemap_t *)data;
  g_return_if_fail (exemap);

  if (exemap->map)
    preload_map_unref (exemap->map);
  g_free (exemap);
}


void
preload_exe_foreach_exemap (preload_exe_t *exe, GFunc func, gpointer user_data)
{
  g_return_if_fail (exe);
  g_ptr_array_foreach (exe->exemaps, func, user_data);
}


static void
exe_add_map_size (preload_exemap_t *exemap, preload_exe_t *exe)
{
  exe->size += preload_map_get_size (exemap->map);
}


preload_exe_t *
preload_exe_new (const char *path, gboolean running, GPtrArray *exemaps)
{
  preload_exe_t *exe;

  g_return_val_if_fail (path, NULL);

  exe = g_malloc (sizeof (*exe));
  exe->path = g_strdup (path);
  exe->size = 0;
  exe->time = 0;
  exe->change_timestamp = state->time;
  if (running) {
    exe->update_time = exe->running_timestamp = state->last_running_timestamp;
  } else {
    exe->update_time = exe->running_timestamp = -1;
  }
  if (!exemaps)
    exe->exemaps = g_ptr_array_new ();
  else
    exe->exemaps = exemaps;
  g_ptr_array_foreach (exe->exemaps, (GFunc)exe_add_map_size, exe);
  exe->markovs = g_ptr_array_new ();
  return exe;
}


void
preload_exe_free (preload_exe_t *exe)
{
  g_return_if_fail (exe);

  if (exe->exemaps) {
    g_ptr_array_foreach (exe->exemaps, (GFunc)preload_exemap_free, NULL);
    g_ptr_array_free (exe->exemaps, TRUE);
    exe->exemaps = NULL;
  }
  if (exe->markovs) {
    g_ptr_array_foreach (exe->markovs, (GFunc)preload_markov_free, exe);
    g_ptr_array_free (exe->markovs, TRUE);
    exe->markovs = NULL;
  }
  if (exe->path) {
    g_free (exe->path);
    exe->path = NULL;
  }
  g_free (exe);
}


preload_exemap_t *
preload_exemap_new_from_exe (preload_exe_t *exe, preload_map_t *map)
{
  preload_exemap_t *exemap;

  g_return_val_if_fail (exe, NULL);
  g_return_val_if_fail (map, NULL);

  exemap = preload_exemap_new (map);
  g_ptr_array_add (exe->exemaps, exemap);
  exe_add_map_size (exemap, exe);
  return exemap;
}


static void
shift_preload_markov_new (gpointer G_GNUC_UNUSED key, preload_exe_t *a, preload_exe_t *b)
{
  if (a != b)
    preload_markov_new (a, b, TRUE);
}


void
preload_state_register_exe (preload_exe_t *exe, gboolean create_markovs)
{
  g_return_if_fail (state);
  g_return_if_fail (state->exes);

  g_return_if_fail (exe && exe->path);
  g_return_if_fail (!g_hash_table_lookup (state->exes, exe->path));

  exe->seq = ++(state->exe_seq);
  if (create_markovs && state->exes) {
    g_hash_table_foreach (state->exes, (GHFunc)shift_preload_markov_new, exe);
  }
  g_hash_table_insert (state->exes, exe->path, exe);
}


void
preload_state_unregister_exe (preload_exe_t *exe)
{
  g_return_if_fail (state && state->exes);
  g_return_if_fail (exe && exe->path);
  g_return_if_fail (g_hash_table_lookup (state->exes, exe->path));

  g_hash_table_steal (state->exes, exe->path);

  preload_exe_free (exe);
}
