/* spy.c - preload data acquisation routines
 *
 * Copyright (C) 2005  Behdad Esfahbod
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
#include "spy.h"
#include "conf.h"
#include "state.h"
#include "proc.h"
#include "vomm.h"


static GSList *state_changed_exes;
static GSList *new_running_exes;
static GHashTable *new_exes;


/* for every process, check whether we know what it is, and add it
 * to appropriate list for further analysis. */
static void
running_process_callback (gpointer key, gpointer G_GNUC_UNUSED value, gpointer G_GNUC_UNUSED user_data)
{
  pid_t pid = (pid_t)GPOINTER_TO_INT(value);
  const char *path = (const char *)key;

  preload_exe_t *exe;

  g_return_if_fail (path);

  exe = g_hash_table_lookup (state->exes, path);
  if (exe) {
    /* already existing exe */

    /* has it been running already? */
    if (!exe_is_running (exe)) {
      new_running_exes = g_slist_prepend (new_running_exes, exe);
      state_changed_exes = g_slist_prepend (state_changed_exes, exe);

      /* VOMM Update Hook: Record execution event (transition from idle to running) */
      if (preload_is_vomm_algorithm()) {
          vomm_update(exe);
      }
    }

    /* update timestamp */
    exe->running_timestamp = state->time;

  } else if (!g_hash_table_lookup (state->bad_exes, path)) {

    /* an exe we have never seen before, just queue it */
    g_hash_table_insert (new_exes, g_strdup (path), GUINT_TO_POINTER (pid));
  }
}


static void
already_running_exe_callback (gpointer data, gpointer G_GNUC_UNUSED user_data)
{
  preload_exe_t *exe = (preload_exe_t *)data;

  if (exe_is_running (exe))
    new_running_exes = g_slist_prepend (new_running_exes, exe);
  else
    state_changed_exes = g_slist_prepend (state_changed_exes, exe);
}


static void
new_exe_callback (gpointer key, gpointer value, gpointer G_GNUC_UNUSED user_data)
{
  char *path = (char *)key;
  pid_t pid = (pid_t)GPOINTER_TO_INT(value);

  gboolean want_it;
  size_t size;

  size = proc_get_maps (pid, NULL, NULL);

  if (!size) /* process died or something */
    return;

  want_it = size >= (size_t)conf->model.minsize;

  if (want_it) {
    preload_exe_t *exe;
    GPtrArray *exemaps;

    size = proc_get_maps (pid, state->maps, &exemaps);
    if (!size) {
      /* process just died, clean up */
      g_ptr_array_foreach (exemaps, (GFunc)preload_exemap_free, NULL);
      g_ptr_array_free (exemaps, TRUE);
      return;
    }

    exe = preload_exe_new (path, TRUE, exemaps);
    preload_state_register_exe (exe, TRUE);
    state->running_exes = g_slist_prepend (state->running_exes, exe);

    /* VOMM Update Hook: Record execution event (newly discovered process) */
    if (preload_is_vomm_algorithm()) {
        vomm_update(exe);
    }

  } else {
    g_hash_table_insert (state->bad_exes, g_strdup (path), GINT_TO_POINTER (size));
  }
}


static void
running_markov_inc_time (gpointer data, gpointer user_data)
{
  preload_markov_t *markov = (preload_markov_t *)data;
  int time = GPOINTER_TO_INT(user_data);

  if (markov->state == 3)
    markov->time += time;
}

static void
running_exe_inc_time (gpointer G_GNUC_UNUSED key, gpointer value, gpointer user_data)
{
  preload_exe_t *exe = (preload_exe_t *)value;
  int time = GPOINTER_TO_INT(user_data);

  if (exe_is_running (exe))
    exe->time += time;
}


static void
exe_changed_callback (gpointer data, gpointer G_GNUC_UNUSED user_data)
{
  preload_exe_t *exe = (preload_exe_t *)data;

  exe->change_timestamp = state->time;
  g_ptr_array_foreach (exe->markovs, (GFunc)preload_markov_state_changed, NULL);
}







void
preload_spy_scan (gpointer data)
{
  /* scan processes, see which exes started running, which are not running
   * anymore, and what new exes are around. */

  state_changed_exes = new_running_exes = NULL;
  new_exes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* mark each running exe with fresh timestamp */
  proc_foreach ((GHFunc)running_process_callback, data);
  state->last_running_timestamp = state->time;

  /* figure out who's not running by checking their timestamp */
  g_slist_foreach (state->running_exes, (GFunc)already_running_exe_callback, data);

  g_slist_free (state->running_exes);
  state->running_exes = new_running_exes;
}

/* update_model is run after scan, after some delay (half a cycle) */

void
preload_spy_update_model (gpointer data)
{
  int period;

  /* register newly discovered exes */
  g_hash_table_foreach (new_exes, (GHFunc)new_exe_callback, data);
  g_hash_table_destroy (new_exes);

  /* and adjust states for those changing */
  g_slist_foreach (state_changed_exes, (GFunc)exe_changed_callback, data);
  g_slist_free (state_changed_exes);

  /* do some accounting */
  period = state->time - state->last_accounting_timestamp;
  g_hash_table_foreach (state->exes, (GHFunc)running_exe_inc_time, GINT_TO_POINTER (period));
  preload_markov_foreach ((GFunc)running_markov_inc_time, GINT_TO_POINTER (period));
  state->last_accounting_timestamp = state->time;
}
