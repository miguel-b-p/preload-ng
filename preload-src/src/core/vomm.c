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

#include <math.h>

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
    g_debug("Initializing VOMM Algorithm...");
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
    
    g_debug("VOMM Update: %s", exe->path);

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
            g_debug("VOMM: Bigram updated %s -> %s", prev_exe->path, exe->path);
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
        
        /* Avoid log(0) */
        if (child_conf > 0.99) child_conf = 0.99;
        
        child->exe->lnprob += log(1.0 - child_conf);
        g_debug("VOMM PPM: Bidding on %s with conf %.2f", child->exe->path, child_conf);
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
             child->exe->lnprob += log(0.9);
             g_debug("VOMM: Fallback bidding on %s with lnprob += %f", child->exe->path, log(0.9));
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
                g_debug("VOMM Global: Bidding on %s with global_conf %.3f (count=%d)", 
                        child->exe->path, global_conf, child->count);
            }
        }
    }
}

void vomm_predict(void) {
    if (!vomm_system.root) {
        g_debug("VOMM: No root context for prediction");
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
            g_debug("VOMM: Predicting from history item: %s (has %d children)", 
                    hist_exe->path, g_hash_table_size(global_ctx->children));
            predict_ppm(global_ctx);
            predictions_made++;
        }
    }
    
    /* Also try current_context if it has children (deep context prediction) */
    if (vomm_system.current_context && 
        vomm_system.current_context != vomm_system.root &&
        g_hash_table_size(vomm_system.current_context->children) > 0) {
        g_debug("VOMM: Predicting from deep context (Order K)");
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
    g_debug("VOMM: Applying global frequency predictions");
    predict_global_frequency();
    
    if (predictions_made == 0) {
        g_debug("VOMM: No context predictions - relying on global frequency only");
    } else {
        g_debug("VOMM: Made predictions from %d contexts + global frequency", predictions_made);
    }
}
