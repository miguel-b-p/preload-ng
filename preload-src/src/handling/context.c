#include "context.h"
#include "preload.h" /* For defaults */
#include "common.h"

preload_ctx_t *
preload_context_new(void)
{
  preload_ctx_t *ctx = g_new0(preload_ctx_t, 1);
  
  ctx->conffile = g_strdup(DEFAULT_CONFFILE);
  ctx->statefile = g_strdup(DEFAULT_STATEFILE);
  ctx->logfile = g_strdup(DEFAULT_LOGFILE);
  ctx->nicelevel = DEFAULT_NICELEVEL;
  ctx->foreground = FALSE;
  
  /* Link to existing globals for now */
  ctx->conf = conf;
  ctx->state = state;
  
  return ctx;
}

void
preload_context_free(preload_ctx_t *ctx)
{
  if (!ctx) return;
  
  g_free(ctx->conffile);
  g_free(ctx->statefile);
  g_free(ctx->logfile);
  
  if (ctx->main_loop)
      g_main_loop_unref(ctx->main_loop);

  g_free(ctx);
}
