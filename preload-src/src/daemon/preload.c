/* preload.c - preload daemon body
 *
 * Copyright (C) 2005  Behdad Esfahbod
 * Portions Copyright (C) 2000  Andrew Henroid
 * Portions Copyright (C) 2001  Sun Microsystems (thockin@sun.com)
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
#include "preload.h"
#include "log.h"
#include "cmdline.h"
#include "conf.h"
#include "state.h"
#include "context.h"

#include <signal.h>
#include <grp.h>

/* variables */

/* variables */

static preload_ctx_t *ctx = NULL;

/* local variables */



/* local variables */





/* Functions */

static void
daemonize (void)
{
  switch (fork ())
    {
    case -1:
      g_error ("fork failed, exiting: %s", strerror (errno));
      exit (EXIT_FAILURE);
      break;
    case 0:
      /* child */
      break;
    default:
      /* parent */
      if (getpid () == 1)
        {
          /* chain to /sbin/init if we are called as init! */
          execl ("/sbin/init", "init", NULL);
          execl ("/bin/init", "init", NULL);
        }
      exit (EXIT_SUCCESS);
    }

  /* disconnect */
  setsid ();
  umask (0007);

  /* get outta the way */
  (void) chdir ("/");
}


/* signal handling */


static gboolean
sig_handler_sync (gpointer data)
{
  switch (GPOINTER_TO_INT (data)) {
    case SIGHUP:
      preload_conf_load (ctx->conffile, FALSE);
      preload_log_reopen (ctx->logfile);
      break;
    case SIGUSR1:
      preload_state_dump_log ();
      preload_conf_dump_log ();
      break;
    case SIGUSR2:
      preload_state_save (ctx->statefile);
      break;
    default: /* everything else is an exit request */
      g_message ("exit requested");
      g_main_loop_quit (ctx->main_loop);
      break;
  }
  return FALSE;
}

static void
sig_handler (int sig)
{
  g_timeout_add (0, sig_handler_sync, GINT_TO_POINTER (sig));
}

static void
set_sig_handlers (void)
{
  /* trap key signals */
  signal (SIGINT,  sig_handler);
  signal (SIGQUIT, sig_handler);
  signal (SIGTERM, sig_handler);
  signal (SIGHUP,  sig_handler);
  signal (SIGUSR1, sig_handler);
  signal (SIGUSR2, sig_handler);
  signal (SIGPIPE, SIG_IGN);
}


int
main (int argc, char **argv)
{
  /* initialize */
  /* initialize */
  ctx = preload_context_new();
  
  preload_cmdline_parse (ctx, &argc, &argv);
  
  preload_log_init (ctx->logfile);
  preload_conf_load (ctx->conffile, TRUE);
  set_sig_handlers ();
  if (!ctx->foreground)
    daemonize ();
  if (0 > nice (ctx->nicelevel))
    g_warning ("%s", strerror (errno));
  g_debug ("starting up");
  preload_state_load (ctx->statefile);

  /* main loop */
  ctx->main_loop = g_main_loop_new (NULL, FALSE);
  preload_state_run (ctx->statefile);
  g_main_loop_run (ctx->main_loop);

  /* clean up */
  preload_state_save (ctx->statefile);
  if (preload_is_debugging ())
    preload_state_free ();
  g_debug ("exiting");
  
  preload_context_free(ctx);
  return EXIT_SUCCESS;
}
