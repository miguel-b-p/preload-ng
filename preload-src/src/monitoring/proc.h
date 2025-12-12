#ifndef PROC_H
#define PROC_H

/* preload_memory_t: structure holding information
 * about memory conditions of the system. */
typedef struct _preload_memory_t
{
  /* runtime: */

  /* all the following are in kilobytes (1024) */

  int total;	/* total memory */
  int free;	/* free memory */
  int buffers;	/* buffers memory (block device metadata) */
  int cached;	/* page-cache memory (file contents) */

  /* LRU list breakdown (Linux 2.6.28+) */
  int active;         /* recently accessed pages */
  int inactive;       /* less recently accessed pages */
  int active_anon;    /* active anonymous pages (heap, stack) */
  int inactive_anon;  /* inactive anonymous pages */
  int active_file;    /* active file-backed pages */
  int inactive_file;  /* inactive file-backed pages */

  /* Available memory (Linux 3.14+, more accurate than free+cached) */
  int available;      /* estimate of available memory for new allocations */

  int pagein;	/* total data paged (read) in since boot */
  int pageout;	/* total data paged (written) out since boot */

} preload_memory_t;

/* read system memory information */
void proc_get_memstat (preload_memory_t *mem);

/* returns sum of length of maps, in bytes, or 0 if failed */
size_t proc_get_maps (pid_t pid, GHashTable *maps, GPtrArray **exemaps);

/* foreach process running, passes pid as key and exe path as value */
void proc_foreach (GHFunc func, gpointer user_data);

#endif
