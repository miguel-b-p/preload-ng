/* proc.c - preload process listing routines
 *
 * Copyright (C) 2005  Behdad Esfahbod
 * Copyright (C) 2004  Soeren Sandmann (sandmann@daimi.au.dk)
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
#include "proc.h"
#include "conf.h"
#include "state.h"

#include <dirent.h>
#include <ctype.h>

/* now here is the nasty stuff:  ideally we want to ignore/get-rid-of
 * deleted binaries and maps, BUT, preLINK, renames and later deletes
 * them all the time, to replace them with (supposedly better) prelinked
 * ones.  if we go by ignoring deleted files, we end up losing all precious
 * information we have gathered.  can't afford that.  what we do is to
 * consider the renamed file as the original file.  in other words, when
 * prelink renames /bin/bash to /bin/bash.#prelink#.12345, we will behave
 * just like it was still /bin/bash.  that almost solves the problem.
 * fortunately prelink does not change map offset/length, only fills in
 * the linking addresses.
 */

static gboolean
sanitize_file (char *file)
{
  char *p;

  if (*file != '/') /* not a file-backed object */
    return FALSE;

  /* get rid of prelink foo and accept it */
  p = strstr (file, ".#prelink#.");
  if (p) {
    *p = '\0';
    return TRUE;
  }

  /* and (non-prelinked) deleted files */
  if (strstr (file, "(deleted)"))
    return FALSE;

  return TRUE;
}

static gboolean
accept_file (char *file, char * const *prefix)
{
  if (prefix)
    for (; *prefix; prefix++) {
      const char *p = *prefix;
      gboolean accept;
      if (*p == '!') {
	p++;
	accept = FALSE;
      } else {
	accept = TRUE;
      }
      if (!strncmp (file, p, strlen (p)))
	return accept;
    }

  /* accept if no match */
  return TRUE;
}

size_t
proc_get_maps (pid_t pid, GHashTable *maps, GPtrArray **exemaps)
{
  char name[32];
  FILE *in;
  size_t size = 0;
  char buffer[1024];

  if (exemaps)
    *exemaps = g_ptr_array_new ();
  
  g_snprintf (name, sizeof (name), "/proc/%d/maps", pid);
  in = fopen (name, "r");
  if (!in)
    {
      /* this may fail for a variety of reason.  process terminated
       * for example, or permission denied. */
      return 0;
    }
  
  while (fgets (buffer, sizeof (buffer) - 1, in))
    {
      char file[FILELEN];
      long start, end, offset, length;
      int count;

      count = sscanf (buffer, "%lx-%lx %*15s %lx %*x:%*x %*u %"FILELENSTR"s",
		      &start, &end, &offset, file);

      if (count != 4 || !sanitize_file (file) || !accept_file (file, conf->system.mapprefix))
        continue;

      length = end - start;
      size += length;

      if (maps || exemaps) {
	gpointer orig_map;
	preload_map_t *map;
	gpointer value;
	gboolean map_is_new = FALSE;  /* Track if we own this map */

	map = preload_map_new (file, offset, length);

	if (maps) {
	  if (g_hash_table_lookup_extended (maps, map, &orig_map, &value)) {
	    preload_map_free (map);
	    map = (preload_map_t *)orig_map;
	  } else {
	    map_is_new = TRUE;  /* We created a new map that wasn't in the table */
	  }
	} else {
	  map_is_new = TRUE;  /* No maps table, we own this map */
	}
	  
	if (exemaps) {
	  preload_exemap_t *exemap;
	  exemap = preload_exemap_new (map);  /* This takes a ref */
	  g_ptr_array_add (*exemaps, exemap);
	  /* exemap now owns the reference, so we're done with it */
	} else if (map_is_new) {
	  /* No exemaps and map is new (not from maps table) - we need to free it
	   * to avoid memory leak. This happens when maps != NULL but lookup fails
	   * and exemaps == NULL. */
	  preload_map_free (map);
	}
      }
    }

  fclose (in);

  return size;
}

static gboolean
all_digits (const char *s)
{
  for (; *s; ++s)
  {
      if (!isdigit (*s))
	  return FALSE;
  }
  return TRUE;
}

void
proc_foreach (GHFunc func, gpointer user_data)
{
  DIR *proc;
  struct dirent *entry;
  pid_t selfpid = getpid ();

  proc = opendir ("/proc");
  if (!proc)
    g_error ("failed opening /proc: %s", strerror (errno));

  while ((entry = readdir (proc)))
  {
      if (entry->d_name && all_digits (entry->d_name))
      {
	  pid_t pid;
	  char name[32];
	  char exe_buffer[FILELEN];
	  int len;

	  pid = atoi (entry->d_name);
	  if (pid == selfpid)
	    continue;

	  g_snprintf (name, sizeof (name) - 1, "/proc/%s/exe", entry->d_name);

	  len = readlink (name, exe_buffer, sizeof (exe_buffer));

	  if (len <= 0 /* error occured */
	      || len == sizeof (exe_buffer) /* name didn't fit completely */)
	    continue;

	  exe_buffer[len] = '\0';

	  if (!sanitize_file (exe_buffer) || !accept_file (exe_buffer, conf->system.exeprefix))
	    continue;

	  func (GUINT_TO_POINTER (pid), exe_buffer, user_data);
      }
  }
  
  closedir (proc);
}




#define open_file(filename) G_STMT_START {			\
	  int fd, len;						\
	  len = 0;						\
	  if ((fd = open(filename, O_RDONLY)) != -1) {		\
	    if ((len = read(fd, buf, sizeof (buf) - 1)) < 0)	\
	      len = 0;						\
	    close (fd);						\
	  }							\
	  buf[len] = '\0';					\
	} G_STMT_END

#define read_tag(tag, v) G_STMT_START {				\
	  const char *b;					\
	  b = strstr(buf, tag" ");				\
	  if (b) sscanf(b, tag" %d", &(v));			\
	} G_STMT_END

#define read_tag2(tag, v1, v2) G_STMT_START {			\
	  const char *b;					\
	  b = strstr(buf, tag" ");				\
	  if (b) sscanf(b, tag" %d %d", &(v1), &(v2));		\
	} G_STMT_END

void
proc_get_memstat (preload_memory_t *mem)
{
  static int pagesize = 0;
  char buf[4096];

  memset (mem, 0, sizeof (*mem));

  if (!pagesize)
    pagesize = getpagesize ();
 
  open_file ("/proc/meminfo");
  read_tag ("MemTotal:", mem->total);
  read_tag ("MemFree:", mem->free);
  read_tag ("MemAvailable:", mem->available);
  read_tag ("Buffers:", mem->buffers);
  read_tag ("Cached:", mem->cached);
  
  /* LRU list breakdown - available since Linux 2.6.28 */
  read_tag ("Active:", mem->active);
  read_tag ("Inactive:", mem->inactive);
  read_tag ("Active(anon):", mem->active_anon);
  read_tag ("Inactive(anon):", mem->inactive_anon);
  read_tag ("Active(file):", mem->active_file);
  read_tag ("Inactive(file):", mem->inactive_file);

  open_file ("/proc/vmstat");
  read_tag ("pgpgin", mem->pagein);
  read_tag ("pgpgout", mem->pageout);

  if (!mem->pagein) {
    open_file ("/proc/stat");
    read_tag2 ("page", mem->pagein, mem->pageout);
  }

  mem->pagein *= pagesize / 1024;
  mem->pageout *= pagesize / 1024;

  /* Fallback for MemAvailable (pre Linux 3.14) */
  if (!mem->available && mem->free) {
    /* Approximate: free + (cached - min cache) + (buffers portion) 
     * This is a rough estimate; kernel's calculation is more complex */
    mem->available = mem->free + (mem->cached * 80 / 100) + (mem->buffers * 75 / 100);
  }

  if (!mem->total || !mem->pagein)
    g_warning ("failed to read memory stat, is /proc mounted?");
}
