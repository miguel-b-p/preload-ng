/* state.c - Preload state orchestrator
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
#include "log.h"
#include "state.h"
#include "state_io.h"
#include "conf.h"
#include "proc.h"
#include "spy.h"
#include "prophet.h"
#include "vomm.h"
#include "model_utils.h"


/* Global state singleton */
preload_state_t state[1];


/* Callback to set running processes after state load */
static void
set_running_process_callback (gpointer key, gpointer value, gpointer user_data)
{
  pid_t pid = (pid_t)GPOINTER_TO_INT(key);
  const char *path = (const char *)value;
  int time = GPOINTER_TO_INT(user_data);
  (void)pid;

  preload_exe_t *exe;

  exe = g_hash_table_lookup (state->exes, path);
  if (exe) {
    exe->running_timestamp = time;
    state->running_exes = g_slist_prepend (state->running_exes, exe);
    
    /* VOMM Update Hook: Record execution event */
    if (preload_is_vomm_algorithm()) {
        vomm_update(exe);
    }
  }
}


void
preload_state_load (const char *statefile)
{
  char *errmsg;
  
  memset (state, 0, sizeof (*state));
  state->exes = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)preload_exe_free);
  state->bad_exes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  state->maps = g_hash_table_new ((GHashFunc)preload_map_hash, (GEqualFunc)preload_map_equal);
  state->maps_arr = g_ptr_array_new ();

  if (statefile && *statefile) {
    errmsg = preload_state_read_file (statefile);
    if (errmsg) {
      g_error ("failed loading state: %s", errmsg);
      g_free (errmsg);
    }
  }

  /* Initialize running processes */
  proc_foreach ((GHFunc)set_running_process_callback, GINT_TO_POINTER (state->time));
  state->last_running_timestamp = state->time;

  proc_get_memstat (&(state->memstat));
  state->memstat_timestamp = state->time;
}


static gboolean
true_func (gpointer G_GNUC_UNUSED key, gpointer G_GNUC_UNUSED value, gpointer G_GNUC_UNUSED user_data)
{
  return TRUE;
}


void
preload_state_save (const char *statefile)
{
  if (state->dirty && statefile && *statefile) {
    char *errmsg = preload_state_write_file (statefile);
    if (errmsg) {
      g_critical ("failed saving state: %s", errmsg);
      g_free (errmsg);
    } else {
      state->dirty = FALSE;
    }
  }

  /* Clean up deleted executables/maps from model */
  preload_cleanup_invalid_entries(state->exes, state->maps);

  /* clean up bad exes once in a while */
  g_hash_table_foreach_remove (state->bad_exes, (GHRFunc)true_func, NULL);
}


void
preload_state_free (void)
{
  g_message ("freeing state memory begin");
  g_hash_table_destroy (state->bad_exes);
  state->bad_exes = NULL;
  g_hash_table_destroy (state->exes);
  state->exes = NULL;
  g_assert (g_hash_table_size (state->maps) == 0);
  g_assert (state->maps_arr->len == 0);
  g_hash_table_destroy (state->maps);
  state->maps = NULL;
  g_slist_free (state->running_exes);
  state->running_exes = NULL;
  g_ptr_array_free (state->maps_arr, TRUE);
  vomm_cleanup();
  g_debug ("freeing state memory done");
}


void
preload_state_dump_log (void)
{
  g_message ("state log dump requested");
  fprintf (stderr, "persistent state stats:\n");
  fprintf (stderr, "preload time = %d\n", state->time);
  fprintf (stderr, "num exes = %d\n", g_hash_table_size (state->exes));
  fprintf (stderr, "num bad exes = %d\n", g_hash_table_size (state->bad_exes));
  fprintf (stderr, "num maps = %d\n", g_hash_table_size (state->maps));
  fprintf (stderr, "runtime state stats:\n");
  fprintf (stderr, "num running exes = %d\n", g_slist_length (state->running_exes));
  g_debug ("state log dump done");
}




/* Tick scheduling */

static gboolean preload_state_tick (gpointer data);


static gboolean
preload_state_tick2 (gpointer data)
{
  if (state->model_dirty) {
    g_debug ("state updating begin");
    preload_spy_update_model (data);
    state->model_dirty = FALSE;
    g_debug ("state updating end");
  }

  /* increase time and reschedule */
  state->time += (conf->model.cycle + 1) / 2;
  g_timeout_add_seconds ((conf->model.cycle + 1) / 2, preload_state_tick, data);
  return FALSE;
}


static gboolean
preload_state_tick (gpointer data)
{
  if (conf->system.doscan) {
    g_debug ("state scanning begin");
    preload_spy_scan (data);
    if (preload_is_debugging())
      preload_state_dump_log ();
    state->dirty = state->model_dirty = TRUE;
    g_debug ("state scanning end");
  }
  if (conf->system.dopredict) {
    g_debug ("state predicting begin");
    preload_prophet_predict (data);
    g_debug ("state predicting end");
  }

  /* increase time and reschedule */
  state->time += conf->model.cycle / 2;
  g_timeout_add_seconds (conf->model.cycle / 2, preload_state_tick2, data);
  return FALSE;
}



static const char *autosave_statefile;


static gboolean
preload_state_autosave (gpointer G_GNUC_UNUSED user_data)
{
  preload_state_save (autosave_statefile);

  g_timeout_add_seconds (conf->system.autosave, (GSourceFunc)preload_state_autosave, NULL);
  return FALSE;
}


void
preload_state_run (const char *statefile)
{
  if (preload_is_vomm_algorithm()) {
      if (!vomm_init()) {
          g_warning("Failed to initialize VOMM algorithm");
      } else {
          /* VOMM starts empty; hydrate it from the loaded legacy state */
          vomm_hydrate_from_state();
      }
  }
  g_timeout_add (0, preload_state_tick, NULL);
  if (statefile) {
    autosave_statefile = statefile;
    g_timeout_add_seconds (conf->system.autosave, (GSourceFunc)preload_state_autosave, NULL);
  }
}
