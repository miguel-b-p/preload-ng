#ifndef STATE_H
#define STATE_H

#include <proc.h>

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



/* preload_exemap_t: structure holding information
 * about a mapped section in an exe. */
typedef struct _preload_exemap_t
{
  preload_map_t *map;
  double prob; /* probability that this map is used when exe is running. */
} preload_exemap_t;



/* preload_exe_t: structure holding information
 * about an executable. */
typedef struct _preload_exe_t
{
  char *path; /* absolute path of the executable. */
  int time; /* total time that this has been running, ever. */
  int update_time; /* last time it was probed. */
  GPtrArray *markovs; /* set of markov chains with other exes. */
  GPtrArray *exemaps; /* set of exemap structures. */

  /* runtime: */
  size_t size; /* sum of the size of the maps, in bytes. */
  int running_timestamp; /* last time it was running. */
  int change_timestamp; /* time started/stopped running. */
  double lnprob; /* log-probability of NOT being needed in next period. */
  int seq; /* unique exe sequence number. */
} preload_exe_t;
#define exe_is_running(exe) ((exe)->running_timestamp >= state->last_running_timestamp)



/* preload_markov_t: a 4-state continuous-time Markov chain. */
typedef struct _preload_markov_t
{
  preload_exe_t *a, *b; /* involved exes. */
  int time; /* total time that both exes have been running simultaneously (state 3). */
  double time_to_leave[4]; /* mean time to leave each state. */
  int weight[4][4]; /* number of times we've gone from state i to state j.
		     * weight[i][i] is the number of times we have left
		     * state i. (sum over weight[i][j] for j<>i essentially. */

  /* runtime: */
  /* state 0: no-a, no-b,
   * state 1:    a, no-b,
   * state 2: no-a,    b,
   * state 3:    a,    b.
   */
  int state; /* current state */
  int change_timestamp; /* time entered the current state. */
} preload_markov_t;

#define markov_other_exe(markov,exe) ((markov)->a == (exe) ? (markov)->b : (markov)->a)
#define markov_state(markov) ((exe_is_running((markov)->a)?1:0)+(exe_is_running((markov)->b)?2:0))



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




/* state */

extern preload_state_t state[1];

void preload_state_load (const char *statefile);
void preload_state_save (const char *statefile);
void preload_state_dump_log (void);
void preload_state_run (const char *statefile);
void preload_state_free (void);
void preload_state_register_exe (preload_exe_t *exe, gboolean create_markovs);
void preload_state_unregister_exe (preload_exe_t *exe);



/* map */

/* duplicates path */
preload_map_t * preload_map_new (const char *path, size_t offset, size_t length);
void preload_map_free (preload_map_t *map);
void preload_map_ref (preload_map_t *map);
void preload_map_unref (preload_map_t *map);
size_t preload_map_get_size (preload_map_t *map);
guint preload_map_hash (preload_map_t *map);
gboolean preload_map_equal (preload_map_t *a, preload_map_t *b);



/* exemap */

preload_exemap_t * preload_exemap_new (preload_map_t *map);
void preload_exemap_free (gpointer data, gpointer user_data);
void preload_exemap_foreach (GHFunc func, gpointer user_data);



/* markov */

preload_markov_t * preload_markov_new (preload_exe_t *a, preload_exe_t *b, gboolean initialize);
void preload_markov_free (preload_markov_t *markov, preload_exe_t *from);
void preload_markov_state_changed (preload_markov_t *markov);
double preload_markov_correlation (preload_markov_t *markov);
void preload_markov_foreach (GFunc func, gpointer user_data);



/* exe */

/* duplicates path */
preload_exe_t * preload_exe_new (const char *path, gboolean running, GPtrArray *exemaps);
void preload_exe_free (preload_exe_t *);
preload_exemap_t * preload_exe_map_new (preload_exe_t *exe, preload_map_t *map);



#endif
