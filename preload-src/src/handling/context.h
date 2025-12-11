#ifndef PRELOAD_CONTEXT_H
#define PRELOAD_CONTEXT_H

#include <glib.h>
#include "state.h"
#include "conf.h"

typedef struct _preload_ctx_t {
    /* Configuration Paths */
    char *conffile;
    char *statefile;
    char *logfile;
    
    /* Settings */
    int nicelevel;
    gboolean foreground;
    
    /* Runtime */
    GMainLoop *main_loop;
    
    /* Pointers to Core Structures */
    /* Eventually these shouldn't be globals, but owned here */
    preload_conf_t *conf; 
    preload_state_t *state; 

} preload_ctx_t;

preload_ctx_t *preload_context_new(void);
void preload_context_free(preload_ctx_t *ctx);

#endif /* PRELOAD_CONTEXT_H */
