/* vomm.c - Variable Order Markov Model (Hybrid VOMM + DG) implementation
 *
 * Copyright (C) 2025  Preload-NG Team
 *
 * This file is part of preload.
 */

#include "common.h"
#include "vomm.h"
#include "log.h"
#include "conf.h"
#include "prophet.h"
#include "state.h"
#include "exe.h"

#include <math.h>
#include "state_io.h" /* For string writing macros if needed, or we just write raw strings */


/* VOMM Node representing a context in the prediction tree */
struct _vomm_node_t {
    preload_exe_t *exe;
    GHashTable *children;       /* Transitions to next states (Key: exe path string, Value: vomm_node_t*)
                                 * Hash table owns both keys and values; set destroy functions on creation */
    int count;
    struct _vomm_node_t *parent;
};

/* Global VOMM System State (opaque to external code) */
struct _vomm_system_t {
    vomm_node_t *root;               /* Root of the prediction tree */
    vomm_node_t *current_context;    /* Pointer to the current node in the tree based on recent history */
    GList *history;                 /* Recent execution history (for identifying context) - DO NOT MODIFY EXTERNALLY */
    guint history_length;           /* Length of the history list (tracked for O(1) access) - DO NOT MODIFY EXTERNALLY */
};

static struct _vomm_system_t vomm_system = {0};

/* Forward declaration */
static void vomm_node_free(gpointer data);

/* Helper: Create a new node */
static vomm_node_t* vomm_node_new(preload_exe_t *exe, vomm_node_t *parent) {
    vomm_node_t *node = g_malloc0(sizeof(vomm_node_t));
    node->exe = exe;
    node->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, vomm_node_free);
    node->count = 0;
    node->parent = parent;
    return node;
}

/* Helper: Free a node recursively */
static void vomm_node_free(gpointer data) {
    vomm_node_t *node = (vomm_node_t*)data;
    if (!node) return;
    g_hash_table_destroy(node->children);
    g_free(node);
}

gboolean vomm_init(void) {
    g_debug("[VOMM] Initializing Algorithm...");
    /* Root represents the empty context */
    vomm_system.root = vomm_node_new(NULL, NULL);
    vomm_system.current_context = vomm_system.root;
    vomm_system.history = NULL;
    vomm_system.history_length = 0;
    return TRUE;
}

void vomm_cleanup(void) {
    if (vomm_system.root) {
        vomm_node_free(vomm_system.root);
    }
    g_list_free(vomm_system.history);
    
    /* Reset global state to avoid dangling pointers */
    vomm_system.root = NULL;
    vomm_system.history = NULL;
    vomm_system.current_context = NULL;
    vomm_system.history_length = 0;
}

/* 
 * Update mechanism:
 * In a full VOMM, we would store every sequence. For this hybrid version,
 * we update the tree by extending the current path.
 * We limit the depth to prevent infinite growth (e.g., max order 5).
 */
#define MAX_VOMM_DEPTH 5

void vomm_update(preload_exe_t *exe) {
    if (!exe) return;
    
    /* VOMM might not be initialized yet during state load */
    if (!vomm_system.root) return;
    
    g_debug("[VOMM] Update: %s", exe->path);

    /* 1. Update Global History */
    vomm_system.history = g_list_append(vomm_system.history, exe);
    vomm_system.history_length++;
    
    if (vomm_system.history_length > MAX_VOMM_DEPTH) {
        /* Prune oldest entry (head of the list) */
        /* Use g_list_delete_link to remove the specific node without O(N) scan */
        vomm_system.history = g_list_delete_link(vomm_system.history, vomm_system.history); 
        vomm_system.history_length--;
    }

    /* 2. Update Tree Structure (Training) */
    /* For simplicity in this hybrid model, we just follow the current context 
       and add the new node if it doesn't exist, incrementing count. 
       In a "pure" VOMM we'd insert all suffixes, but here we act more like a PST/PSA. */
    
    /* Reset to root if we are too deep or lost */
    if (!vomm_system.current_context) {
        vomm_system.current_context = vomm_system.root;
    }

    vomm_node_t *next_node = g_hash_table_lookup(vomm_system.current_context->children, exe->path);
    if (!next_node) {
        next_node = vomm_node_new(exe, vomm_system.current_context);
        g_hash_table_insert(vomm_system.current_context->children, g_strdup(exe->path), next_node);
    }

    next_node->count++;
    
    /* Move context forward */
    vomm_system.current_context = next_node;

    /* 3. Global Bigram Update (Order 1) */
    /* Also update Root -> [Exe] -> [NewExe] to capture global pair transitions */
    if (vomm_system.history_length >= 2) {
        GList *last_link = g_list_last(vomm_system.history);
        /* last_link is current exe, last_link->prev is previous exe */
        if (last_link && last_link->prev) {
            preload_exe_t *prev_exe = (preload_exe_t *)last_link->prev->data;
            
            vomm_node_t *root_ctx = g_hash_table_lookup(vomm_system.root->children, prev_exe->path);
            if (!root_ctx) {
                root_ctx = vomm_node_new(prev_exe, vomm_system.root);
                g_hash_table_insert(vomm_system.root->children, g_strdup(prev_exe->path), root_ctx);
            }
            
            vomm_node_t *bigram_target = g_hash_table_lookup(root_ctx->children, exe->path);
            if (!bigram_target) {
                bigram_target = vomm_node_new(exe, root_ctx);
                g_hash_table_insert(root_ctx->children, g_strdup(exe->path), bigram_target);
            }
            bigram_target->count++;
            g_debug("[VOMM] Bigram updated: %s -> %s", prev_exe->path, exe->path);
        }
    }
}

/* PPM Prediction Logic */
static void predict_ppm(vomm_node_t *node) {
    GHashTableIter iter;
    gpointer key, value;
    int total_children_calls = 0;
    vomm_node_t *best_child = NULL;
    int max_count = -1;

    g_hash_table_iter_init(&iter, node->children);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        vomm_node_t *child = (vomm_node_t*)value;
        total_children_calls += child->count;
        
        if (child->count > max_count) {
            max_count = child->count;
            best_child = child;
        }
    }

    if (!best_child || total_children_calls == 0) return;

    /* Aggressive Prediction: Bid on all candidates based on probability */
    /* No threshold check: User requested "preload anyway" */
    
    g_hash_table_iter_init(&iter, node->children);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        vomm_node_t *child = (vomm_node_t*)value;
        
        /* Skip executables that are already running - they're in memory */
        if (!child->exe || exe_is_running(child->exe)) continue;
        
        double child_conf = (double)child->count / total_children_calls;
        
        /* Clamp child_conf to a safe range to avoid log(0) */
        const double epsilon = 1e-9;
        child_conf = fmax(epsilon, fmin(1.0 - epsilon, child_conf));
        
        child->exe->lnprob += log(child_conf);
        g_debug("[VOMM] PPM Prediction: Bidding on %s (conf: %.4f)", child->exe->path, child_conf);
    }
}

/* Fallback DG Prediction Logic */
static void predict_dg_fallback(vomm_node_t *node) {
     /* 
      * Layer 2: Generalist Fallback
      * Just look at the direct children of the current node (Transition Probability 1-order)
      * This works as a Dependency Graph on the immediate previous state.
      */
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, node->children);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        vomm_node_t *child = (vomm_node_t*)value;
        
        /* Skip executables that are already running */
        if (!child->exe || exe_is_running(child->exe)) continue;
        
        if (child->count > 0) {
             /* Weak bid for all neighbors */
             /* Add a small probability boost */
             child->exe->lnprob += log(1.1);
             g_debug("[VOMM] Fallback Prediction: Bidding on %s (lnprob += %f)", child->exe->path, log(1.1));
        }
    }
}

/* Global frequency-based prediction: bid on ALL known executables */
static void predict_global_frequency(void) {
    GHashTableIter ctx_iter, child_iter;
    gpointer ctx_key, ctx_value, child_key, child_value;
    int total_global_count = 0;
    
    /* First pass: calculate total counts across all transitions */
    g_hash_table_iter_init(&ctx_iter, vomm_system.root->children);
    while (g_hash_table_iter_next(&ctx_iter, &ctx_key, &ctx_value)) {
        vomm_node_t *ctx_node = (vomm_node_t*)ctx_value;
        
        g_hash_table_iter_init(&child_iter, ctx_node->children);
        while (g_hash_table_iter_next(&child_iter, &child_key, &child_value)) {
            vomm_node_t *child = (vomm_node_t*)child_value;
            total_global_count += child->count;
        }
    }
    
    if (total_global_count == 0) return;
    
    /* Second pass: bid on all known executables based on their frequency */
    g_hash_table_iter_init(&ctx_iter, vomm_system.root->children);
    while (g_hash_table_iter_next(&ctx_iter, &ctx_key, &ctx_value)) {
        vomm_node_t *ctx_node = (vomm_node_t*)ctx_value;
        
        g_hash_table_iter_init(&child_iter, ctx_node->children);
        while (g_hash_table_iter_next(&child_iter, &child_key, &child_value)) {
            vomm_node_t *child = (vomm_node_t*)child_value;
            
            /* Skip executables that are already running */
            if (!child->exe || exe_is_running(child->exe)) continue;
            
            if (child->count > 0) {
                /* Global frequency-based confidence (weaker than context-specific) */
                double global_conf = (double)child->count / total_global_count;
                
                /* Apply a dampening factor since this is less specific than context prediction */
                /* Scale to range [0.1, 0.5] to avoid overpowering context predictions */
                global_conf = 0.1 + (global_conf * 0.4);
                if (global_conf > 0.5) global_conf = 0.5;
                
                child->exe->lnprob += log(1.0 - global_conf);
                /* g_debug("VOMM Global: Bidding on %s with global_conf %.3f (count=%d)", 
                        child->exe->path, global_conf, child->count); */
            }
        }
    }
}

void vomm_predict(void) {
    if (!vomm_system.root) {
        g_debug("[VOMM] No root context for prediction");
        return;
    }
    
    /* Hybrid Prediction Strategy */
    
    /* 
     * LAYER 1: Context-Specific Prediction (Order 1 Bigrams)
     * 
     * The root->children contains nodes for each "previous" executable.
     * Each of those nodes has children representing "next" executables.
     * This captures: "After running X, user often runs Y"
     * 
     * We look at ALL recent history items and predict from each.
     */
    
    GList *hist_iter;
    int predictions_made = 0;
    
    /* Iterate through recent history and predict from each context */
    for (hist_iter = vomm_system.history; hist_iter != NULL; hist_iter = hist_iter->next) {
        preload_exe_t *hist_exe = (preload_exe_t *)hist_iter->data;
        if (!hist_exe || !hist_exe->path) continue;
        
        vomm_node_t *global_ctx = g_hash_table_lookup(vomm_system.root->children, hist_exe->path);
        if (global_ctx && g_hash_table_size(global_ctx->children) > 0) {
            g_debug("[VOMM] Predicting from history item: %s (has %d children)", 
                    hist_exe->path, g_hash_table_size(global_ctx->children));
            predict_ppm(global_ctx);
            predictions_made++;
        }
    }
    
    /* Also try current_context if it has children (deep context prediction) */
    if (vomm_system.current_context && 
        vomm_system.current_context != vomm_system.root &&
        g_hash_table_size(vomm_system.current_context->children) > 0) {
        g_debug("[VOMM] Predicting from deep context (Order K)");
        predict_ppm(vomm_system.current_context);
        predict_dg_fallback(vomm_system.current_context);
        predictions_made++;
    }
    
    /*
     * LAYER 2: Global Frequency Fallback
     * 
     * Bid on ALL known executables based on their total observed frequency.
     * This ensures we preload commonly-used programs even if they're not
     * direct children of the current context.
     */
    g_debug("[VOMM] Applying global frequency predictions");
    predict_global_frequency();
    
    if (predictions_made == 0) {
        g_debug("[VOMM] No context predictions - relying on global frequency only");
    } else {
        g_debug("[VOMM] Made predictions from %d contexts + global frequency", predictions_made);
    }
}

/* 
 * Hydrate VOMM model from legacy Markov state 
 * This allows VOMM to work immediately after restart by using the 
 * persistent markov weights as a seed for the VOMM tree.
 */
void vomm_hydrate_from_state(void) {
    if (!vomm_system.root) return;
    
    g_debug("[VOMM] Hydrating from legacy Markov state...");
    int hydrated_count = 0;
    
    GHashTableIter iter;
    gpointer key, value;
    
    /* Iterate over all known executables */
    g_hash_table_iter_init(&iter, state->exes);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        preload_exe_t *exe = (preload_exe_t *)value;
        if (!exe || !exe->markovs) continue;
        
        /* Ensure 'exe' exists in the root context */
        vomm_node_t *root_ctx_exe = g_hash_table_lookup(vomm_system.root->children, exe->path);
        
        /* Iterate over all markovs involving this executable */
        int i;
        for (i = 0; i < exe->markovs->len; i++) {
            preload_markov_t *mk = g_ptr_array_index(exe->markovs, i);
            
            /* Determine the "other" executable in the pair */
            preload_exe_t *other = (mk->a == exe) ? mk->b : mk->a;
            if (!other) continue;
            
            /* 
             * Check transition A -> B
             * In legacy markov:
             * state 1 = A running, no B
             * state 3 = A running, B running (transition from 1->3 means B started while A was running)
             */
            
            int count = 0;
            preload_exe_t *src = NULL;
            preload_exe_t *dst = NULL;

            /* Case 1: exe is A, other is B. We want A -> B (1 -> 3) */
            if (mk->a == exe) {
                 src = exe;
                 dst = other;
                 count = mk->weight[1][3];
            } 
            /* Case 2: exe is B, other is A. We want B -> A (2 -> 3) */
            else {
                 src = exe;
                 dst = other;
                 count = mk->weight[2][3];
            }
            
            if (count > 0) {
                 /* 
                  * Add transition src -> dst to VOMM tree 
                  * Root -> [src] -> [dst]
                  */
                 
                 /* 1. Ensure src exists in Root children */
                 vomm_node_t *src_node = g_hash_table_lookup(vomm_system.root->children, src->path);
                 if (!src_node) {
                     src_node = vomm_node_new(src, vomm_system.root);
                     g_hash_table_insert(vomm_system.root->children, g_strdup(src->path), src_node);
                 }
                 
                 /* 2. Ensure dst exists in src children (The Transition) */
                 vomm_node_t *dst_node = g_hash_table_lookup(src_node->children, dst->path);
                 if (!dst_node) {
                     dst_node = vomm_node_new(dst, src_node);
                     g_hash_table_insert(src_node->children, g_strdup(dst->path), dst_node);
                 }
                 
                 /* 3. Add the count (hydrate) */
                 /* Only add if it looks like it needs hydration (avoid double counting if called multiple times, though typically called once) */
                 /* Since count is cumulative in markov, and we are just setting up, we can just ADD */
                 /* But be careful not to over-inflate if we persist VOMM counts separately later. 
                    For now VOMM isn't persisted, so this is safe. */
                 dst_node->count += count;
                 
                 hydrated_count++;
                 /* g_debug("VOMM: Hydrated %s -> %s (count=%d)", src->path, dst->path, count); */
            }
        }
    }
    
    g_debug("[VOMM] Hydration complete. Imported %d transitions.", hydrated_count);
}

/* --- Persistence Implementation --- */

/* Node ID counter for export */
static gint64 export_node_id_counter = 0;

/* Helper to recursively write nodes */
static void vomm_node_export_recursive(vomm_node_t *node, gint64 parent_id, VommNodeWriter writer, gpointer user_data) {
    if (!node) return;

    /* Assign ID to this node */
    gint64 current_id = ++export_node_id_counter;

    /* Write this node if it's not the root */
    if (node != vomm_system.root) {
        if (node->exe) {
            writer(current_id, node->exe->seq, node->count, parent_id, user_data);
        }
    } else {
        /* Root is ID 0 */
        current_id = 0;
    }

    /* Recurse children */
    if (node->children) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, node->children);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            vomm_node_t *child = (vomm_node_t*)value;
            vomm_node_export_recursive(child, current_id, writer, user_data);
        }
    }
}

void vomm_export_state(VommNodeWriter writer, gpointer user_data) {
    g_debug("[VOMM] Exporting state...");
    export_node_id_counter = 0;
    /* Reset root ID to 0 logically */
    vomm_node_export_recursive(vomm_system.root, -1, writer, user_data);
}


/* Import state logic */
static GHashTable *import_node_map = NULL;

void vomm_import_node(gint64 id, preload_exe_t *exe, int count, gint64 parent_id) {
    if (!vomm_system.root) vomm_init(); // Ensure initialized

    if (import_node_map == NULL) {
        import_node_map = g_hash_table_new(g_direct_hash, g_direct_equal);
        /* Add root as ID 0 */
        g_hash_table_insert(import_node_map, GINT_TO_POINTER(0), vomm_system.root);
    }

    vomm_node_t *parent_node = g_hash_table_lookup(import_node_map, GINT_TO_POINTER((gint)parent_id));
    if (!parent_node) {
        g_warning("[VOMM] Orphan node id=%" G_GINT64_FORMAT ", parent=%" G_GINT64_FORMAT " not found. Skipping.", id, parent_id);
        return;
    }

    if (!exe) {
        g_warning("[VOMM] Node id=%" G_GINT64_FORMAT " has no exe. Skipping.", id);
        return;
    }

    /* Create the node */
    vomm_node_t *node = vomm_node_new(exe, parent_node);
    node->count = count;

    /* Add to parent's children hash table */
    /* Key is exe->path (string) */
    /* Note: if using full VOMM, might allow multiple children with same exe but differentiating by something else? 
       No, VOMM/PST structure is unique by edge label (exe) from a parent. */
    g_hash_table_insert(parent_node->children, g_strdup(exe->path), node);

    /* Remember this node for future children */
    g_hash_table_insert(import_node_map, GINT_TO_POINTER((gint)id), node);
}

void vomm_import_done(void) {
    if (import_node_map) {
        g_hash_table_destroy(import_node_map);
        import_node_map = NULL;
    }
    g_debug("[VOMM] Import complete.");
}
