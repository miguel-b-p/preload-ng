/* log.c - preload logging routines
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
#include "log.h"
#include "preload.h"

#include <time.h>

int preload_log_level = DEFAULT_LOGLEVEL;

static void
preload_log (const char *log_domain, GLogLevelFlags log_level,
	     const char *message, gpointer G_GNUC_UNUSED user_data)
{
  time_t curtime;
  char *timestr;

  /* ignore unimportant messages */
  if (log_level <= G_LOG_LEVEL_ERROR << preload_log_level)
    {
      curtime = time (NULL);
      timestr = ctime (&curtime);
      timestr[strlen (timestr) - 1] = '\0';
      fprintf (stderr, "[%s] %s%s%s\n", timestr,
	       log_domain ? log_domain : "", log_domain ? ": " : "", message);
    }

  if (log_level & G_LOG_FLAG_FATAL) {
    /*int *p;*/
    preload_log (log_domain, 0, "Exiting", NULL);
    fflush (stdout);
    fflush (stderr);
    /* make a segfault */
    /*
    p = NULL;
    *p = 0;
    */
    exit (EXIT_FAILURE);
  }
}

void
preload_log_init (const char *logfile)
{
  if (logfile && *logfile)
    {
      int logfd;
      int nullfd;

      /* g_message ("opening log file %s", logfile); */

      /* set up stdout, stderr to log and stdin to /dev/null */

      if (0 > (nullfd = open ("/dev/null", O_RDONLY)))
	g_error ("cannot open %s: %s", "/dev/null", strerror (errno));

      if (0 > (logfd = open (logfile, O_WRONLY | O_CREAT | O_APPEND, 0600)))
	g_error ("cannot open %s: %s", logfile, strerror (errno));

      if ((dup2 (nullfd, STDIN_FILENO) != STDIN_FILENO) ||
	  (dup2 (logfd, STDOUT_FILENO) != STDOUT_FILENO) ||
	  (dup2 (logfd, STDERR_FILENO) != STDERR_FILENO))
	g_error ("dup2: %s", strerror (errno));

      close (nullfd);
      close (logfd);

      /* g_debug ("opening log file done"); */
    }

  g_log_set_default_handler (preload_log, NULL);
}

void
preload_log_reopen (const char *logfile)
{
  int logfd;

  if (!(logfile && *logfile))
    return;

  g_message ("reopening log file %s", logfile);

  fflush (stdout);
  fflush (stderr);

  if (0 > (logfd = open (logfile, O_WRONLY | O_CREAT | O_APPEND, 0600))) {
    g_warning ("cannot reopen %s: %s", logfile, strerror (errno));
    return;
  }

  if ((dup2 (logfd, STDOUT_FILENO) != STDOUT_FILENO) ||
      (dup2 (logfd, STDERR_FILENO) != STDERR_FILENO))
    g_warning ("dup2: %s", strerror (errno));

  close (logfd);

  g_message ("reopening log file %s done", logfile);
}
