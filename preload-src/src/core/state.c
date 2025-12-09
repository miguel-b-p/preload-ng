/* state.c - preload persistent state handling routines
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
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
#include "conf.h"
#include "proc.h"
#include "spy.h"
#include "prophet.h"
#include "model_utils.h"

#include <math.h>

/* horrible hack to shut the double-declaration of g_snprintf up may
 * be removed after development is done.  pretty harmless though. */
#define __G_PRINTF_H__

#include <glib/gstdio.h>


preload_state_t state[1];





preload_map_t *
preload_map_new (const char *path, size_t offset, size_t length)
{
  preload_map_t *map;

  g_return_val_if_fail (path, NULL);

  map = g_malloc (sizeof (*map));
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


void
preload_map_ref (preload_map_t *map)
{
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
  return a->offset == b->offset && a->length == b->length && !strcmp (a->path, b->path);
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

typedef struct _exemap_foreach_context_t
{
  preload_exe_t *exe;
  GHFunc func;
  gpointer data;
} exemap_foreach_context_t;

static void
exe_exemap_callback (preload_exemap_t *exemap, exemap_foreach_context_t *ctx)
{
  ctx->func (exemap, ctx->exe, ctx->data);
}

static void
exe_exemap_foreach (gpointer G_GNUC_UNUSED key, preload_exe_t *exe, exemap_foreach_context_t *ctx)
{
  ctx->exe = exe;
  g_ptr_array_foreach (exe->exemaps, (GFunc)exe_exemap_callback, ctx);
}

void
preload_exemap_foreach (GHFunc func, gpointer user_data)
{
  exemap_foreach_context_t ctx;
  ctx.func = func;
  ctx.data = user_data;
  g_hash_table_foreach (state->exes, (GHFunc)exe_exemap_foreach, &ctx);
}



preload_markov_t *
preload_markov_new (preload_exe_t *a, preload_exe_t *b, gboolean initialize)
{
  preload_markov_t *markov;

  g_return_val_if_fail (a, NULL);
  g_return_val_if_fail (b, NULL);
  g_return_val_if_fail (a != b, NULL);

  markov = g_malloc (sizeof (*markov));
  markov->a = a;
  markov->b = b;
  if (initialize) {

    markov->state = markov_state (markov);

    markov->change_timestamp = state->time;
    if (a->change_timestamp > 0 && b->change_timestamp > 0) {
      if (a->change_timestamp < state->time)
	markov->change_timestamp = a->change_timestamp;
      if (b->change_timestamp < state->time && b->change_timestamp > markov->change_timestamp)
	markov->change_timestamp = b->change_timestamp;  /* Fixed: was incorrectly 'a' */
      if (a->change_timestamp > markov->change_timestamp)
	markov->state ^= 1;
      if (b->change_timestamp > markov->change_timestamp)
	markov->state ^= 2;
    }

    markov->time = 0;
    memset (markov->time_to_leave, 0, sizeof (markov->time_to_leave));
    memset (markov->weight, 0, sizeof (markov->weight));
    preload_markov_state_changed (markov);
  }
  g_ptr_array_add (a->markovs, markov);
  g_ptr_array_add (b->markovs, markov);
  return markov;
}


void
preload_markov_state_changed (preload_markov_t *markov)
{
  int old_state, new_state;

  if (markov->change_timestamp == state->time)
    return; /* already taken care of */

  old_state = markov->state;
  new_state = markov_state (markov);

  g_return_if_fail (old_state != new_state);

  markov->weight[old_state][old_state]++;
  markov->time_to_leave[old_state] += ((state->time - markov->change_timestamp)
				       - markov->time_to_leave[old_state])
				      / markov->weight[old_state][old_state];

  markov->weight[old_state][new_state]++;
  markov->state = new_state;
  markov->change_timestamp = state->time;
}


void
preload_markov_free (preload_markov_t *markov, preload_exe_t *from)
{
  g_return_if_fail (markov);

  if (from) {
    preload_exe_t *other;
    g_assert (markov->a == from || markov->b == from);
    other = markov_other_exe (markov, from);
    g_ptr_array_remove_fast (other->markovs, markov);
  } else {
    g_ptr_array_remove_fast (markov->a->markovs, markov);
    g_ptr_array_remove_fast (markov->b->markovs, markov);
  }
  g_free (markov);
}


typedef struct _markov_foreach_context_t
{
  preload_exe_t *exe;
  GFunc func;
  gpointer data;
} markov_foreach_context_t;

static void
exe_markov_callback (preload_markov_t *markov, markov_foreach_context_t *ctx)
{
  /* each markov should be processed only once, not twice */
  if (ctx->exe == markov->a)
    ctx->func (markov, ctx->data);
}

static void
exe_markov_foreach (gpointer G_GNUC_UNUSED key, preload_exe_t *exe, markov_foreach_context_t *ctx)
{
  ctx->exe = exe;
  g_ptr_array_foreach (exe->markovs, (GFunc)exe_markov_callback, ctx);
}

void
preload_markov_foreach (GFunc func, gpointer user_data)
{
  markov_foreach_context_t ctx;
  ctx.func = func;
  ctx.data = user_data;
  g_hash_table_foreach (state->exes, (GHFunc)exe_markov_foreach, &ctx);
}


/* calculate the correlation coefficient of the two random variable of
 * the exes in this markov been running.
 *
 * the returned value is a number in the range -1 to 1 that is a numeric
 * measure of the strength of linear relationship between two random
 * variables.  the correlation is 1 in the case of an increasing linear
 * relationship, −1 in the case of a decreasing linear relationship, and
 * some value in between in all other cases, indicating the degree of
 * linear dependence between the variables.  the closer the coefficient
 * is to either −1 or 1, the stronger the correlation between the variables.
 * see:
 *
 *   http://en.wikipedia.org/wiki/Correlation
 *
 * we calculate the Pearson product-moment correlation coefficient, which
 * is found by dividing the covariance of the two variables by the product
 * of their standard deviations.  that is:
 *
 *
 *                  E(AB) - E(A)E(B)
 *   ρ(a,b) = ___________________________
 *             ____________  ____________
 *            √ E(A²)-E²(A) √ E(B²)-E²(B)
 *
 *
 * Where A and B are the random variables of exes a and b being run, with
 * a value of 1 when running, and 0 when not.  It's obvious to compute the
 * above then, since:
 *
 *   E(AB) = markov->time / state->time
 *   E(A) = markov->a->time / state->time
 *   E(A²) = E(A)
 *   E²(A) = E(A)²
 *   (same for B)
 */
double
preload_markov_correlation (preload_markov_t *markov)
{
  double correlation, numerator, denominator2;
  int t, a, b, ab;
  
  t = state->time;
  a = markov->a->time;
  b = markov->b->time;
  ab = markov->time;

  if (a == 0 || a == t || b == 0 || b == t)
    correlation = 0;
  else {
    numerator = ((double)t*ab) - ((double)a * b);
    denominator2 = ((double)a * b) * ((double)(t - a) * (t - b));
    correlation = numerator / sqrt (denominator2);
  }
  
  g_assert (fabs (correlation) <= 1.00001);
  return correlation;
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
preload_exe_map_new (preload_exe_t *exe, preload_map_t *map)
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
  g_return_if_fail (exe && exe->path);
  g_return_if_fail (!g_hash_table_lookup (state->exes, exe->path));

  exe->seq = ++(state->exe_seq);
  if (create_markovs) {
    g_hash_table_foreach (state->exes, (GHFunc)shift_preload_markov_new, exe);
  }
  g_hash_table_insert (state->exes, exe->path, exe);
}


void
preload_state_unregister_exe (preload_exe_t *exe)
{
  g_return_if_fail (exe && exe->path);
  g_return_if_fail (g_hash_table_lookup (state->exes, exe->path));

  if (exe->markovs) {
    g_ptr_array_foreach (exe->markovs, (GFunc)preload_markov_free, exe);
    g_ptr_array_free (exe->markovs, TRUE);
    exe->markovs = NULL;
  }
  g_hash_table_remove (state->exes, exe->path);
}







#define TAG_PRELOAD     "PRELOAD"
#define TAG_MAP         "MAP"
#define TAG_BADEXE      "BADEXE"
#define TAG_EXE         "EXE"
#define TAG_EXEMAP      "EXEMAP"
#define TAG_MARKOV      "MARKOV"







#define READ_TAG_ERROR			"invalid tag"
#define READ_SYNTAX_ERROR		"invalid syntax"
#define READ_INDEX_ERROR		"invalid index"
#define READ_DUPLICATE_INDEX_ERROR	"duplicate index"
#define READ_DUPLICATE_OBJECT_ERROR	"duplicate object"

typedef struct _read_context_t
{
  char *line;
  const char *errmsg;
  char *path;
  GHashTable *maps;
  GHashTable *exes;
  gpointer data;
  GError *err;
  char filebuf[FILELEN];
} read_context_t;




static void
read_map (read_context_t *rc)
{
  preload_map_t *map;
  int update_time;
  int i, expansion;
  long offset, length;
  char *path;

  if (6 > sscanf (rc->line,
		  "%d %d %lu %lu %d %"FILELENSTR"s",
		  &i, &update_time, &offset, &length, &expansion, rc->filebuf)) {
    rc->errmsg = READ_SYNTAX_ERROR;
    return;
  }

  path = g_filename_from_uri (rc->filebuf, NULL, &(rc->err));
  if (!path)
    return;

  map = preload_map_new (path, offset, length);
  g_free (path);
  if (g_hash_table_lookup (rc->maps, GINT_TO_POINTER (i))) {
    rc->errmsg = READ_DUPLICATE_INDEX_ERROR;
    goto err;
  }
  if (g_hash_table_lookup (state->maps, map)) {
    rc->errmsg = READ_DUPLICATE_OBJECT_ERROR;
    goto err;
  }

  map->update_time = update_time;
  preload_map_ref (map);
  g_hash_table_insert (rc->maps, GINT_TO_POINTER (i), map);
  return;

err:
  /* Cannot use preload_map_free since refcount is 0 and g_return_if_fail will fail.
   * Manually free the map struct and its path string. */
  g_free (map->path);
  g_free (map);
}


static void
read_badexe (read_context_t *rc)
{
  int size;
  int expansion;
  char *path;

  /* we do not read-in badexes.  let's clean them up on every start, give them
   * another chance! */
  return;

  if (3 > sscanf (rc->line,
		  "%d %d %"FILELENSTR"s",
		  &size, &expansion, rc->filebuf)) {
    rc->errmsg = READ_SYNTAX_ERROR;
    return;
  }

  path = g_filename_from_uri (rc->filebuf, NULL, &(rc->err));
  if (!path)
    return;

  g_hash_table_insert (state->bad_exes, path, GINT_TO_POINTER (size));
}


static void
read_exe (read_context_t *rc)
{
  preload_exe_t *exe;
  int update_time, time;
  int i, expansion;
  char *path;

  if (5 > sscanf (rc->line,
		  "%d %d %d %d %"FILELENSTR"s",
		  &i, &update_time, &time, &expansion, rc->filebuf)) {
    rc->errmsg = READ_SYNTAX_ERROR;
    return;
  }

  path = g_filename_from_uri (rc->filebuf, NULL, &(rc->err));
  if (!path)
    return;

  exe = preload_exe_new (path, FALSE, NULL);
  exe->change_timestamp = -1;
  g_free (path);
  if (g_hash_table_lookup (rc->exes, GINT_TO_POINTER (i))) {
    rc->errmsg = READ_DUPLICATE_INDEX_ERROR;
    goto err;
  }
  if (g_hash_table_lookup (state->exes, exe->path)) {
    rc->errmsg = READ_DUPLICATE_OBJECT_ERROR;
    goto err;
  }

  exe->update_time = update_time;
  exe->time = time;
  g_hash_table_insert (rc->exes, GINT_TO_POINTER (i), exe);
  preload_state_register_exe (exe, FALSE);
  return;

err:
  preload_exe_free (exe);
}


static void
read_exemap (read_context_t *rc)
{
  int iexe, imap;
  preload_exe_t *exe;
  preload_map_t *map;
  preload_exemap_t *exemap;
  double prob;

  if (3 > sscanf (rc->line,
		  "%d %d %lg",
		  &iexe, &imap, &prob)) {
    rc->errmsg = READ_SYNTAX_ERROR;
    return;
  }

  exe = g_hash_table_lookup (rc->exes, GINT_TO_POINTER (iexe));
  map = g_hash_table_lookup (rc->maps, GINT_TO_POINTER (imap));
  if (!exe || !map) {
    rc->errmsg = READ_INDEX_ERROR;
    return;
  }

  exemap = preload_exe_map_new (exe, map);
  exemap->prob = prob;
}


static void
read_markov (read_context_t *rc)
{
  int time, state, state_new;
  int ia, ib;
  preload_exe_t *a, *b;
  preload_markov_t *markov;
  int n;

  n = 0;
  if (3 > sscanf (rc->line,
		  "%d %d %d%n",
		  &ia, &ib, &time, &n)) {
    rc->errmsg = READ_SYNTAX_ERROR;
    return;
  }
  rc->line += n;

  a = g_hash_table_lookup (rc->exes, GINT_TO_POINTER (ia));
  b = g_hash_table_lookup (rc->exes, GINT_TO_POINTER (ib));
  if (!a || !b) {
    rc->errmsg = READ_INDEX_ERROR;
    return;
  }

  markov = preload_markov_new (a, b, FALSE);
  markov->time = time;

  for (state = 0; state < 4; state++) {
    double x;
    if (1 > sscanf (rc->line,
		    "%lg%n",
		    &x, &n)) {
      rc->errmsg = READ_SYNTAX_ERROR;
      return;
    }

    rc->line += n;
    markov->time_to_leave[state] = x;
  }
  for (state = 0; state < 4; state++) {
    for (state_new = 0; state_new < 4; state_new++) {
      int x;
      if (1 > sscanf (rc->line,
		      "%d%n",
		      &x, &n)) {
	rc->errmsg = READ_SYNTAX_ERROR;
	return;
      }

      rc->line += n;
      markov->weight[state][state_new] = x;
    }
  }
}


#include "vomm.h"

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

static void
set_markov_state_callback (gpointer data, gpointer G_GNUC_UNUSED user_data)
{
  preload_markov_t *markov = (preload_markov_t *)data;
  markov->state = markov_state (markov);
}

static char *
read_state (GIOChannel *f)
{
  int lineno;
  GString *linebuf;
  GIOStatus s;
  char tag[32] = "";
  char *errmsg;

  read_context_t rc;

  rc.errmsg = NULL;
  rc.err = NULL;
  rc.maps = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)preload_map_unref);
  rc.exes = g_hash_table_new (g_direct_hash, g_direct_equal);

  linebuf = g_string_sized_new (FILELEN + 64);  /* Larger initial buffer to reduce reallocs */
  lineno = 0;

  while (!rc.err && !rc.errmsg) {

    s = g_io_channel_read_line_string (f, linebuf, NULL, &rc.err);
    if (s == G_IO_STATUS_AGAIN)
      continue;
    if (s == G_IO_STATUS_EOF || s == G_IO_STATUS_ERROR)
      break;

    lineno++;
    rc.line = linebuf->str;

    if (1 > sscanf (rc.line,
		    "%31s",
		    tag)) {
      rc.errmsg = READ_TAG_ERROR;
      break;
    }
    rc.line += strlen (tag);

    if (lineno == 1 && strcmp (tag, TAG_PRELOAD)) {
      g_warning ("State file has invalid header, ignoring it");
      break;
    }

    if (!strcmp (tag, TAG_PRELOAD)) {
      int major_ver_read, major_ver_run;
      const char *version;
      int time;

      if (lineno != 1 || 2 > sscanf (rc.line,
				     "%d.%*[^\t]\t%d",
				     &major_ver_read, &time)) {
	rc.errmsg = READ_SYNTAX_ERROR;
	break;
      }

      version = VERSION;
      major_ver_run = strtod (version, NULL);

      if (major_ver_run < major_ver_read ) {
	g_warning ("State file is of a newer version, ignoring it");
	break;
      } else if (major_ver_run > major_ver_read) {
	g_warning ("State file is of an old version that I cannot understand anymore, ignoring it");

	break;
      }

      state->last_accounting_timestamp = state->time = time;
    }
    else if (!strcmp (tag, TAG_MAP))	read_map (&rc);
    else if (!strcmp (tag, TAG_BADEXE))	read_badexe (&rc);
    else if (!strcmp (tag, TAG_EXE))	read_exe (&rc);
    else if (!strcmp (tag, TAG_EXEMAP))	read_exemap (&rc);
    else if (!strcmp (tag, TAG_MARKOV))	read_markov (&rc);
    else if (linebuf->str[0] && linebuf->str[0] != '#') {
      rc.errmsg = READ_TAG_ERROR;
      break;
    }
  }

  g_string_free (linebuf, TRUE);
  g_hash_table_destroy (rc.exes);
  g_hash_table_destroy (rc.maps);

  if (rc.err)
    rc.errmsg = rc.err->message;
  if (rc.errmsg)
    errmsg = g_strdup_printf ("line %d: %s", lineno, rc.errmsg);
  else
    errmsg = NULL;
  if (rc.err)
      g_error_free (rc.err);

  if (!errmsg) {
    proc_foreach ((GHFunc)set_running_process_callback, GINT_TO_POINTER (state->time));
    state->last_running_timestamp = state->time;
    preload_markov_foreach ((GFunc)set_markov_state_callback, NULL);
  }

  return errmsg;
}


void
preload_state_load (const char *statefile)
{
  memset (state, 0, sizeof (*state));
  state->exes = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)preload_exe_free);
  state->bad_exes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  state->maps = g_hash_table_new ((GHashFunc)preload_map_hash, (GEqualFunc)preload_map_equal);
  state->maps_arr = g_ptr_array_new ();

  if (statefile && *statefile)
    {
      GIOChannel *f;
      GError *err = NULL;

      g_message ("loading state from %s", statefile);

      f = g_io_channel_new_file (statefile, "r", &err);
      if (!f) {
	if (err->code == G_FILE_ERROR_ACCES)
	  g_error ("cannot open %s for reading: %s", statefile, err->message);
	else {
	  g_warning ("cannot open %s for reading, ignoring: %s", statefile, err->message);
	  g_error_free (err);
	}
      } else {
        char *errmsg;

	errmsg = read_state (f);
	g_io_channel_unref (f);
	if (errmsg) {
	  g_error ("failed reading state from %s: %s", statefile, errmsg);
	  g_free (errmsg);
	}
      }

      g_debug ("loading state done");
    }

  proc_get_memstat (&(state->memstat));
  state->memstat_timestamp = state->time;
}





#define write_it(s) \
  if (wc->err || G_IO_STATUS_NORMAL != g_io_channel_write_chars (wc->f, s, -1, NULL, &(wc->err))) \
    return;
#define write_tag(tag) write_it (tag "\t")
#define write_string(string) write_it ((string)->str)
#define write_ln() write_it ("\n")

typedef struct _write_context_t
{
  GIOChannel *f;
  GString *line;
  GError *err;
} write_context_t;



static void
write_header (write_context_t *wc)
{
  write_tag (TAG_PRELOAD);
  g_string_printf (wc->line,
		   "%s\t%d",
		   VERSION, state->time);
  write_string (wc->line);
  write_ln ();
}


static void
write_map (preload_map_t *map, gpointer G_GNUC_UNUSED data, write_context_t *wc)
{
  char *uri;

  uri = g_filename_to_uri (map->path, NULL, &(wc->err));
  if (!uri)
    return;

  write_tag (TAG_MAP);
  g_string_printf (wc->line,
		   "%d\t%d\t%lu\t%lu\t%d\t%s",
		   map->seq, map->update_time, (long)map->offset, (long)map->length, -1/*expansion*/, uri);
  write_string (wc->line);
  write_ln ();

  g_free (uri);
}


static void
write_badexe (gpointer key, gpointer value, gpointer user_data)
{
  char *path = (char *)key;
  int update_time = GPOINTER_TO_INT(value);
  write_context_t *wc = (write_context_t *)user_data;

  char *uri;

  uri = g_filename_to_uri (path, NULL, &(wc->err));
  if (!uri)
    return;

  write_tag (TAG_BADEXE);
  g_string_printf (wc->line,
		   "%d\t%d\t%s",
		   update_time, -1/*expansion*/, uri);
  write_string (wc->line);
  write_ln ();

  g_free (uri);
}


static void
write_exe (gpointer G_GNUC_UNUSED key, preload_exe_t *exe, write_context_t *wc)
{
  char *uri;

  uri = g_filename_to_uri (exe->path, NULL, &(wc->err));
  if (!uri)
    return;

  write_tag (TAG_EXE);
  g_string_printf (wc->line,
		   "%d\t%d\t%d\t%d\t%s",
		   exe->seq, exe->update_time, exe->time, -1/*expansion*/, uri);
  write_string (wc->line);
  write_ln ();

  g_free (uri);
}


static void
write_exemap (preload_exemap_t *exemap, preload_exe_t *exe, write_context_t *wc)
{
  write_tag (TAG_EXEMAP);
  g_string_printf (wc->line,
		   "%d\t%d\t%lg",
		   exe->seq, exemap->map->seq, exemap->prob);
  write_string (wc->line);
  write_ln ();
}


static void
write_markov (preload_markov_t *markov, write_context_t *wc)
{
  int state, state_new;

  write_tag (TAG_MARKOV);
  g_string_printf (wc->line,
		   "%d\t%d\t%d",
		   markov->a->seq, markov->b->seq, markov->time);
  write_string (wc->line);

  for (state = 0; state < 4; state++) {
    g_string_printf (wc->line,
		     "\t%lg",
		     markov->time_to_leave[state]);
    write_string (wc->line);
  }
  for (state = 0; state < 4; state++) {
    for (state_new = 0; state_new < 4; state_new++) {
      g_string_printf (wc->line,
		       "\t%d",
		       markov->weight[state][state_new]);
      write_string (wc->line);
    }
  }

  write_ln ();
}


static char *
write_state (GIOChannel *f)
{
  write_context_t wc;

  wc.f = f;
  wc.line = g_string_sized_new (100);
  wc.err = NULL;

  write_header (&wc);
  if (!wc.err) g_hash_table_foreach   (state->maps, (GHFunc)write_map, &wc);
  if (!wc.err) g_hash_table_foreach   (state->bad_exes, (GHFunc)write_badexe, &wc);
  if (!wc.err) g_hash_table_foreach   (state->exes, (GHFunc)write_exe, &wc);
  if (!wc.err) preload_exemap_foreach ((GHFunc)write_exemap, &wc);
  if (!wc.err) preload_markov_foreach ((GFunc)write_markov, &wc);

  g_string_free (wc.line, TRUE);
  if (wc.err) {
    char *tmp;
    tmp = g_strdup (wc.err->message);
    g_error_free (wc.err);
    return tmp;
  } else
    return NULL;
}


static gboolean
true_func (gpointer G_GNUC_UNUSED key, gpointer G_GNUC_UNUSED value, gpointer G_GNUC_UNUSED user_data)
{
  return TRUE;
}


void
preload_state_save (const char *statefile)
{
  if (state->dirty && statefile && *statefile)
    {
      int fd;
      GIOChannel *f;
      char *tmpfile;

      g_message ("saving state to %s", statefile);

      tmpfile = g_strconcat (statefile, ".tmp", NULL);
      g_debug ("to be honest, saving state to %s", tmpfile);

      fd = open (tmpfile, O_WRONLY | O_CREAT | O_TRUNC, 0660);
      if (0 > fd) {
	g_critical ("cannot open %s for writing, ignoring: %s", tmpfile, strerror (errno));
      } else {
        char *errmsg;

        f = g_io_channel_unix_new (fd);

	errmsg = write_state (f);
	g_io_channel_unref (f);
	if (errmsg) {
	  g_critical ("failed writing state to %s, ignoring: %s", tmpfile, errmsg);
	  g_free (errmsg);
	  g_unlink (tmpfile);
	} else {
	  if (0 > g_rename (tmpfile, statefile)) {
	    g_critical ("failed to rename %s to %s", tmpfile, statefile);
	  } else {
	    g_debug ("successfully renamed %s to %s", tmpfile, statefile);
	  }
	}
	close (fd);  /* Fixed: moved inside else block to avoid close(-1) */
      }

      g_free (tmpfile);

      state->dirty = FALSE;

      g_debug ("saving state done");
    }

  /* Clean up deleted executables/maps from model (Task 4: handle deleted files) */
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






/* we divide cycle into two cycle/2 intervals and call tick and tick2 at
 * those.  this is because we like some time to pass before making some
 * decision, so we gather data and make predictions in tick, and use that
 * data to update the model in tick2. */

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
      }
  }
  g_timeout_add (0, preload_state_tick, NULL);
  if (statefile) {
    autosave_statefile = statefile;
    g_timeout_add_seconds (conf->system.autosave, (GSourceFunc)preload_state_autosave, NULL);
  }
}
