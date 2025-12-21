/* test_repro_crash.c - Reproduction test for assertion failure 
 *
 * Simulates the race condition:
 * 1. Exe A is running.
 * 2. Exe B is discovered (New).
 * 3. Exe A stops running (State Change).
 * 
 * IF New is processed BEFORE State Change:
 *    Markov(A,B) created. 
 *    Init: A.change_timestamp is OLD (running).
 *    State change processed: A stops. preload_markov_state_changed called.
 *    Logic: new_state calculation sees A is NOT running.
 *    But markov->state stored A as running (from OLD timestamp).
 *    Result: State mismatch / or if timestamps align in a specific way, redundant update?
 *
 * Actually the specific crash is `old_state != new_state`. 
 * If history reconstruction makes `markov->state` equal to `new_state` prematurely, assertion fails.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "state.h"
#include "exe.h"
#include "markov.h"

/* Mock globals */
preload_state_t state[1];

/* Helper to setup basic state */
static void setup_state() {
    memset(state, 0, sizeof(*state));
    state->exes = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
    state->time = 1000;
    state->last_running_timestamp = 1000;
}

static void teardown_state() {
    if (state->exes) g_hash_table_destroy(state->exes);
}

int main(int argc, char **argv) {
    setup_state();
    printf("Starting reproduction test...\n");

    /* 1. Create Exe A (Existing, Running) */
    preload_exe_t *exe_a = preload_exe_new("/usr/bin/A", TRUE, NULL);
    /* Manually register because we don't have full context */
    state->exe_seq++;
    g_hash_table_insert(state->exes, exe_a->path, exe_a);
    
    /* A started running at time 500 */
    exe_a->running_timestamp = 500;
    state->last_running_timestamp = 1000; /* Current "scan" time */
    
    /* 2. Advance time to 2000. Scan happens. */
    state->time = 2000;
    state->last_running_timestamp = 2000;
    
    /* Scenario: 
     * A stops running.
     * B is discovered (New).
     */

    /* Step 2a: Discover B (New Exe). 
     * In the BUGGY version, this happens FIRST.
     */
    printf("Simulating: New Exe Discovered (B)...\n");
    preload_exe_t *exe_b = preload_exe_new("/usr/bin/B", TRUE, NULL);
    /* In real code: preload_state_register_exe calls markov creation */
    
    /* This creates Markov(A, B).
     * Initialization logic in `preload_markov_new`:
     *   markov->state = compute_state()
     *   markov->change_timestamp = state->time (2000)
     *   Checks A->change_timestamp vs state->time.
     *   
     * CRITICAL: A has NOT been updated yet!
     * A->change_timestamp is still old (e.g. 0 or 500).
     * 
     * exe_is_running(A):
     *   running_timestamp(500) < last_running_timestamp(2000) -> FALSE (Not running).
     *   WAIT! exe_is_running logic: return exe->running_timestamp >= state->last_running_timestamp;
     *   500 >= 2000 is FALSE. Correct.
     * 
     * So Markov initializes identifying A as NOT RUNNING.
     * State = B running (2) + A not running (0) = 2.
     */
    preload_state_register_exe(exe_b, TRUE);
    
    /* Step 2b: Process State Changes (A stops).
     * In real code: `exe_changed_callback`
     */
    printf("Simulating: Exe A State Change (Stops Running)...\n");
    
    /* Update A's timestamp to say it stopped? 
     * Ideally, `running_process_callback` would have NOT seen A running.
     * So A's running_timestamp remains 500.
     * `already_running_exe_callback` detects A is not running.
     * Adds to state_changed_exes.
     */
    
    /* Execute change callback */
    /* exe->change_timestamp = state->time; */
    exe_a->change_timestamp = state->time;
    
    /* foreach markov: preload_markov_state_changed */
    preload_markov_t *markov = g_ptr_array_index(exe_a->markovs, 0); // Get M(A,B)
    
    /* Inside preload_markov_state_changed(markov):
     * old_state = markov->state (2) [B=Running, A=Not Running]
     * new_state = compute_state() [B=Running, A=Not Running] -> 2
     * 
     * ASSERT FAILS: old_state (2) != new_state (2)
     */
     
    printf("Calling preload_markov_state_changed...\n");
    preload_markov_state_changed(markov);
    
    printf("Survivor! (If you see this, the test passed/failed differently than expected)\n");
    
    teardown_state();
    return 0;
}
