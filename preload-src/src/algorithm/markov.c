/* markov.c - Markov chain algorithm for preload prediction
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
#include "markov.h"
#include "exe.h"
#include "state.h"

#include <math.h>

/* Access to global state for timestamps */
extern preload_state_t state[1];


int
markov_compute_state(preload_markov_t *markov)
{
  return (exe_is_running(markov->a) ? 1 : 0) + (exe_is_running(markov->b) ? 2 : 0);
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

    markov->state = markov_compute_state (markov);

    markov->change_timestamp = state->time;
    if (a->change_timestamp > 0 && b->change_timestamp > 0) {
      if (a->change_timestamp < state->time)
	markov->change_timestamp = a->change_timestamp;
      if (b->change_timestamp < state->time && b->change_timestamp > markov->change_timestamp)
	markov->change_timestamp = b->change_timestamp;
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
  new_state = markov_compute_state (markov);

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


/* Markov foreach iteration context */
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
