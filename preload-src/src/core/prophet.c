/* prophet.c - preload inference and prediction routines
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
#include "prophet.h"
#include "log.h"
#include "conf.h"
#include "state.h"
#include "readahead.h"
#include "vomm.h"

#include <math.h>


/* Computes the P(Y runs in next period | current state)
 * and bids in for the Y. Y should not be running.
 *
 * Y=1 if it's needed in next period, 0 otherwise.
 * Probability inference follows:
 *
 *   P(Y=1) = 1 - P(Y=0)
 *   P(Y=0) = Π P(Y=0|Xi)
 *   P(Y=0|Xi) = 1 - P(Y=1|Xi)
 *   P(Y=1|Xi) = P(state change of Y,X) * P(next state has Y=1) * corr(Y,X)
 *   corr(Y=X) = regularized |correlation(Y,X)|
 *
 * So:
 * 
 *   lnprob(Y) = log(P(Y=0)) = Σ log(P(Y=0|Xi)) = Σ log(1 - P(Y=1|Xi))
 *
 */
static void
markov_bid_for_exe (preload_markov_t *markov,
		    preload_exe_t *y,
		    int ystate,
		    double correlation)
{
  int state;
  double p_state_change;
  double p_y_runs_next;
  double p_runs;

  state = markov->state;

  if (!markov->weight[state][state] || !(markov->time_to_leave[state] > 1))
    return;

  /* p_state_change is the probability of the state of markov changing
   * in the next period.  period is taken as 1.5 cycles.  it's computed
   * as:
   *                                            -λ.period
   *   p(state changes in time < period) = 1 - e
   *
   * where λ is one over average time to leave the state.
   */
  p_state_change = -conf->model.cycle * 1.5 / markov->time_to_leave[state];
  p_state_change = 1 - exp (p_state_change);

  /* p_y_runs_next is the probability that X runs, given that a state
   * change occurs. it's computed linearly based on the number of times
   * transition has occured from this state to other states.
   */
  /* regularize a bit by adding something to denominator */
  p_y_runs_next = markov->weight[state][ystate] + markov->weight[state][3];
  p_y_runs_next /= markov->weight[state][state] + 0.01;


  /* FIXME: what should we do we correlation w.r.t. state? */
  correlation = fabs (correlation);

  p_runs = correlation * p_state_change * p_y_runs_next;

  y->lnprob += log (1 - p_runs);
}


static void
markov_bid_in_exes (gpointer data, gpointer G_GNUC_UNUSED user_data)
{
  preload_markov_t *markov = (preload_markov_t *)data;
  double correlation;


  if (!markov->weight[markov->state][markov->state])
    return;

  correlation = conf->model.usecorrelation ? preload_markov_correlation (markov) : 1.0;

  if ((markov->state & 1) == 0) /* a not running */
    markov_bid_for_exe (markov, markov->a, 1, correlation);
  if ((markov->state & 2) == 0) /* b not running */
    markov_bid_for_exe (markov, markov->b, 2, correlation);
}


static void
map_zero_prob (preload_map_t *map, gpointer G_GNUC_UNUSED data)
{
  map->lnprob = 0;
}


static int
map_prob_compare (const preload_map_t **pa, const preload_map_t **pb)
{
  const preload_map_t *a = *pa, *b = *pb;
  return a->lnprob < b->lnprob ? -1 : a->lnprob > b->lnprob ? 1 : 0;
}


static void
exe_zero_prob (gpointer G_GNUC_UNUSED key, preload_exe_t *exe, gpointer G_GNUC_UNUSED data)
{
  exe->lnprob = 0;
}

/* Computes the P(M needed in next period | current state)
 * and bids in for the M, where M is the map used by exemap.
 *
 * M=1 if it's needed in next period, 0 otherwise.
 * Probability inference follows:
 *
 *   P(M=1) = 1 - P(M=0)
 *   P(M=0) = Π P(M=0|Xi)
 *   P(M=0|Xi) = P(Xi=0)
 *
 * So:
 * 
 *   lnprob(M) = log(P(M=0)) = Σ log(P(M=0|Xi)) = Σ log(P(Xi=0)) = Σ lnprob(Xi)
 *
 */
static void
exemap_bid_in_maps (gpointer key, gpointer value, gpointer G_GNUC_UNUSED user_data)
{
  preload_exemap_t *exemap = (preload_exemap_t *)key;
  preload_exe_t *exe = (preload_exe_t *)value;

  if (exe_is_running (exe)) {
    /* if exe is running, we vote against the map,
     * since it's most prolly in the memory already. */
    /* FIXME: use exemap->prob, needs some theory work. */
    exemap->map->lnprob += 1;
  } else {
    exemap->map->lnprob += exe->lnprob;
  }
}


static void exe_prob_print (gpointer G_GNUC_UNUSED key, preload_exe_t *exe, gpointer G_GNUC_UNUSED data) G_GNUC_UNUSED;
static void
exe_prob_print (gpointer G_GNUC_UNUSED key, preload_exe_t *exe, gpointer G_GNUC_UNUSED data)
{
  if (!exe_is_running (exe))
    g_debug ("[Prophet] Final Bid: %s (lnprob: %.4f)", exe->path, exe->lnprob);
}


static void map_prob_print (preload_map_t *map) G_GNUC_UNUSED;
static void
map_prob_print (preload_map_t *map)
{
  g_debug ("[Prophet] Final Bid: %s (lnprob: %.4f)", map->path, map->lnprob);
}


#define clamp_percent(v) ((v)>100 ? 100 : (v) < -100 ? -100 : (v))
#define max(a,b) ((a)>(b) ? (a) : (b))
#define kb(v) ((int)(((v) + 1023) / 1024))

/* input is the list of maps sorted on the need.
 * decide a cutoff based on memory conditions and readhead. */
void
preload_prophet_readahead (GPtrArray *maps_arr)
{
  int i;
  int memavail, memavailtotal; /* in kilobytes */
  preload_memory_t memstat;
  preload_map_t *map;

  proc_get_memstat (&memstat);

  /*
   * Calculate memory available for prefetching.
   *
   * Strategy:
   * 1. If MemAvailable is present (Linux 3.14+), use it as base
   * 2. Otherwise, calculate from free + cached + buffers portions
   *
   * Note on Buffers vs Cached:
   * - Buffers: Block device metadata (filesystem inodes, directory entries)
   *   These are clean copies and can be reclaimed, but kernel needs minimum.
   *   Default: 50% of buffers considered available.
   *
   * - Cached: File content pages (page cache)
   *   Can contain dirty pages that need writeback first.
   *   We use the configured percentage.
   */
  if (memstat.available > 0) {
    /* Linux 3.14+ provides accurate available memory estimate */
    memavail = memstat.available;
    
    /* Apply configured percentage (can be negative to reserve memory) */
    memavail = clamp_percent(conf->model.memtotal) * (memstat.total / 100)
             + clamp_percent(conf->model.memfree)  * (memavail / 100);
  } else {
    /* Fallback for older kernels */
    memavail  = clamp_percent(conf->model.memtotal)  * (memstat.total  / 100)
              + clamp_percent(conf->model.memfree)   * (memstat.free   / 100);
  }
  
  memavail  = max (0, memavail);
  
  /* Add configured portion of cached memory */
  memavail += clamp_percent(conf->model.memcached) * (memstat.cached / 100);
  
  /* Add configured portion of buffers memory */
  memavail += clamp_percent(conf->model.membuffers) * (memstat.buffers / 100);

  memavailtotal = memavail;

  memcpy (&(state->memstat), &memstat, sizeof (memstat));
  state->memstat_timestamp = state->time;

  i = 0;
  while (i < (int)(maps_arr->len) &&
         (map = g_ptr_array_index (maps_arr, i)) &&
	 map->lnprob < 0 && kb (map->length) <= memavail) {
    i++;

    memavail -= kb (map->length);

    if (preload_log_level >= 10)
      map_prob_print (map);
  }

  g_debug ("%dkb available for preloading, using %dkb of it",
	   memavailtotal, memavailtotal - memavail);

  if (i) {
    i = preload_readahead ((preload_map_t **)maps_arr->pdata, i);
    g_debug ("readahead %d files", i);
  } else {
    g_debug ("nothing to readahead");
  }
}


void
preload_prophet_predict (gpointer data)
{
  g_debug("Running Prediction (algorithm: %s)...", 
        conf->system.prediction_algorithm ? conf->system.prediction_algorithm : "NULL");
  if (preload_is_vomm_algorithm()) {
      
      /* Reset probabilities to avoid stale values */
      g_hash_table_foreach (state->exes, (GHFunc)exe_zero_prob, data);
      g_ptr_array_foreach (state->maps_arr, (GFunc)map_zero_prob, data);

      /* For VOMM, we predict based on the last executed file. 
         Ideally, we should track the current sequence context. 
         vomm_predict uses the global context maintained by vomm_update.
         We just pass NULL or current running exe if needed, but vomm_system tracks context.
      */
      vomm_predict();    
      
      /* After VOMM predicts (which modifies lnprobs), we propagate to maps */
      preload_exemap_foreach ((GHFunc)exemap_bid_in_maps, data);

      /* verify: print top exe probabilities for debugging */
      if (preload_log_level >= 9)
        g_hash_table_foreach (state->exes, (GHFunc)exe_prob_print, data);

      g_ptr_array_sort (state->maps_arr, (GCompareFunc)map_prob_compare);
      preload_prophet_readahead (state->maps_arr);
      return;
  }

  /* reset probabilities that we are gonna compute */
  g_hash_table_foreach (state->exes, (GHFunc)exe_zero_prob, data);
  g_ptr_array_foreach (state->maps_arr, (GFunc)map_zero_prob, data);

  /* markovs bid in exes */
  preload_markov_foreach ((GFunc)markov_bid_in_exes, data);

  if (preload_log_level >= 9)
    g_hash_table_foreach (state->exes, (GHFunc)exe_prob_print, data);

  /* exes bid in maps */
  preload_exemap_foreach ((GHFunc)exemap_bid_in_maps, data);

  /* sort maps on probability */
  g_ptr_array_sort (state->maps_arr, (GCompareFunc)map_prob_compare);

  /* read them in */
  preload_prophet_readahead (state->maps_arr);
}
