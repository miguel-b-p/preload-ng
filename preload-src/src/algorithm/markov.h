/* markov.h - Markov chain declarations
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
 * Copyright (C) 2024  Preload-NG Contributors
 *
 * This file is part of preload.
 */

#ifndef MARKOV_H
#define MARKOV_H

#include <glib.h>

/* Forward declarations */
typedef struct _preload_exe_t preload_exe_t;
typedef struct _preload_state_t preload_state_t;

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

/* Macros - need access to exe_is_running which depends on state */
#define markov_other_exe(markov,exe) ((markov)->a == (exe) ? (markov)->b : (markov)->a)

/* Functions */
preload_markov_t * preload_markov_new (preload_exe_t *a, preload_exe_t *b, gboolean initialize);
void preload_markov_free (preload_markov_t *markov, preload_exe_t *from);
void preload_markov_state_changed (preload_markov_t *markov);
double preload_markov_correlation (preload_markov_t *markov);
void preload_markov_foreach (GFunc func, gpointer user_data);

/* Helper to compute current markov state based on running status */
int markov_compute_state(preload_markov_t *markov);

#endif /* MARKOV_H */
