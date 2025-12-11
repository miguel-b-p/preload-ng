/* state_io.c - State persistence for preload
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
#include "state_io.h"
#include "state.h"
#include "map.h"
#include "exe.h"
#include "markov.h"
#include "log.h"

/* horrible hack to shut the double-declaration of g_snprintf up may
 * be removed after development is done.  pretty harmless though. */
#define __G_PRINTF_H__

#include <glib/gstdio.h>


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
  int time, markov_state, state_new;
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

  for (markov_state = 0; markov_state < 4; markov_state++) {
    double x;
    if (1 > sscanf (rc->line,
		    "%lg%n",
		    &x, &n)) {
      rc->errmsg = READ_SYNTAX_ERROR;
      return;
    }

    rc->line += n;
    markov->time_to_leave[markov_state] = x;
  }
  for (markov_state = 0; markov_state < 4; markov_state++) {
    for (state_new = 0; state_new < 4; state_new++) {
      int x;
      if (1 > sscanf (rc->line,
		      "%d%n",
		      &x, &n)) {
	rc->errmsg = READ_SYNTAX_ERROR;
	return;
      }

      rc->line += n;
      markov->weight[markov_state][state_new] = x;
    }
  }
}


static void
set_markov_state_callback (gpointer data, gpointer G_GNUC_UNUSED user_data)
{
  preload_markov_t *markov = (preload_markov_t *)data;
  markov->state = markov_compute_state (markov);
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
      int time_val;

      if (lineno != 1 || 2 > sscanf (rc.line,
				     "%d.%*[^\t]\t%d",
				     &major_ver_read, &time_val)) {
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

      state->last_accounting_timestamp = state->time = time_val;
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
    preload_markov_foreach ((GFunc)set_markov_state_callback, NULL);
  }

  return errmsg;
}


char *
preload_state_read_file (const char *statefile)
{
  GIOChannel *f;
  GError *err = NULL;
  char *errmsg = NULL;

  if (!statefile || !*statefile)
    return NULL;

  g_message ("loading state from %s", statefile);

  f = g_io_channel_new_file (statefile, "r", &err);
  if (!f) {
    if (err->code == G_FILE_ERROR_ACCES) {
      errmsg = g_strdup_printf ("cannot open %s for reading: %s", statefile, err->message);
    } else {
      g_warning ("cannot open %s for reading, ignoring: %s", statefile, err->message);
    }
    g_error_free (err);
    return errmsg;
  }

  errmsg = read_state (f);
  g_io_channel_unref (f);
  
  if (errmsg) {
    char *full_msg = g_strdup_printf ("failed reading state from %s: %s", statefile, errmsg);
    g_free (errmsg);
    return full_msg;
  }

  g_debug ("loading state done");
  return NULL;
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
  int markov_state, state_new;

  write_tag (TAG_MARKOV);
  g_string_printf (wc->line,
		   "%d\t%d\t%d",
		   markov->a->seq, markov->b->seq, markov->time);
  write_string (wc->line);

  for (markov_state = 0; markov_state < 4; markov_state++) {
    g_string_printf (wc->line,
		     "\t%lg",
		     markov->time_to_leave[markov_state]);
    write_string (wc->line);
  }
  for (markov_state = 0; markov_state < 4; markov_state++) {
    for (state_new = 0; state_new < 4; state_new++) {
      g_string_printf (wc->line,
		       "\t%d",
		       markov->weight[markov_state][state_new]);
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


char *
preload_state_write_file (const char *statefile)
{
  int fd;
  GIOChannel *f;
  char *tmpfile;
  char *errmsg = NULL;

  if (!statefile || !*statefile)
    return NULL;

  g_message ("saving state to %s", statefile);

  tmpfile = g_strconcat (statefile, ".tmp", NULL);
  g_debug ("to be honest, saving state to %s", tmpfile);

  fd = open (tmpfile, O_WRONLY | O_CREAT | O_TRUNC, 0660);
  if (0 > fd) {
    errmsg = g_strdup_printf ("cannot open %s for writing: %s", tmpfile, strerror (errno));
    g_free (tmpfile);
    return errmsg;
  }

  f = g_io_channel_unix_new (fd);
  errmsg = write_state (f);
  g_io_channel_unref (f);
  
  if (errmsg) {
    g_unlink (tmpfile);
    close (fd);
    g_free (tmpfile);
    return errmsg;
  }
  
  if (0 > g_rename (tmpfile, statefile)) {
    errmsg = g_strdup_printf ("failed to rename %s to %s", tmpfile, statefile);
    g_unlink (tmpfile);
  } else {
    g_debug ("successfully renamed %s to %s", tmpfile, statefile);
  }
  
  close (fd);
  g_free (tmpfile);
  g_debug ("saving state done");
  
  return errmsg;
}
