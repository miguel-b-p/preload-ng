/* vomm.h - Variable Order Markov Model (Hybrid VOMM + DG) header
 *
 * Copyright (C) 2025  Preload-NG Team
 *
 * This file is part of preload.
 */

#ifndef VOMM_H
#define VOMM_H

#include <glib.h>
#include "state.h"

/* 
 * VOMM Node and System structs are opaque.
 * Use the provided API functions to interact with them.
 * This ensures internal invariants (like history_length) are maintained.
 */
typedef struct _vomm_node_t vomm_node_t;
typedef struct _vomm_system_t vomm_system_t;

/* Core Functions */
gboolean vomm_init(void);
void vomm_cleanup(void);

/* 
 * Updates the VOMM model with a new execution event.
 * Adds the sequence to the tree and updates frequencies.
 *
 * This function stores the exe pointer; caller must ensure exe remains valid until vomm_cleanup.
 * If exe is NULL, the function returns immediately.
 */
void vomm_update(preload_exe_t *exe);

/* 
 * Predicts the next likely files to be needed based on the current context.
 * Uses Hybrid approach:
 * 1. PPM (Prediction by Partial Matching) - Order k
 * 2. Dependency Graph (DG) - Fallback
 */
void vomm_predict(void);

#endif /* VOMM_H */
