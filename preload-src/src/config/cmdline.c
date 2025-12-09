/* cmdline.c - preload command-line options handling routines
 *
 * Copyright (C) 2005,2006,2007  Behdad Esfahbod
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
#include "cmdline.h"
#include "preload.h"

#include <getopt.h>

#define DEFAULT_LOGLEVEL_STRING		STRINGIZE(DEFAULT_LOGLEVEL)
#define DEFAULT_NICELEVEL_STRING	STRINGIZE(DEFAULT_NICELEVEL)

/* TODO: GOption? */

static const struct option const opts[] = {
  {"help", 0, 0, 'h'},
  {"version", 0, 0, 'v'},
  {"conffile", 1, 0, 'c'},
  {"statefile", 1, 0, 's'},
  {"logfile", 1, 0, 'l'},
  {"foreground", 0, 0, 'f'},
  {"nice", 1, 0, 'n'},
  {"verbose", 1, 0, 'V'},
  {"debug", 0, 0, 'd'},
  {NULL, 0, 0, 0},
};

static const char *help2man_str = "Display command line parameters and their default values, and exit.";	/* help2man */
static const char const *opts_help[] = {
  "Display this information and exit.",	/* help */
  "Display version information and exit.",	/* version */
  "Set configuration file. Empty string means no conf file.",	/* conffile */
  "Set state file to load/save. Empty string means no state.",	/* statefile */
  "Set log file. Empty string means to log to stderr.",	/* logfile */
  "Run in foreground, do not daemonize.",	/* foreground */
  "Nice level.",	/* nice */
  "Set the verbosity level.  Levels 0 to 10 are recognized.",	/* verbose */
  "Debug mode: --logfile '' --foreground --verbose 9",	/* debug */
};
static const char const *opts_default[] = {
  NULL,	/* help */
  NULL,	/* version */
  DEFAULT_CONFFILE,	/* conffile */
  DEFAULT_STATEFILE,	/* statefile */
  DEFAULT_LOGFILE,	/* logfile */
  NULL,	/* foreground */
  DEFAULT_NICELEVEL_STRING,	/* nice */
  DEFAULT_LOGLEVEL_STRING,	/* verbose */
  NULL,	/* debug */
};



static void version_func (void) G_GNUC_NORETURN;
static void help_func (gboolean err, gboolean help2man) G_GNUC_NORETURN;


void
preload_cmdline_parse (int *argc, char ***argv)
{
  for (;;)
    {
      int i;
      i = getopt_long (*argc, *argv, "hHvc:s:l:fn:V:d", opts, NULL);
      if (i == -1)
	{
	  break;
	}
      switch (i)
	{
	case 'c':
	  conffile = optarg;
	  break;
	case 's':
	  statefile = optarg;
	  break;
	case 'l':
	  logfile = optarg;
	  break;
	case 'f':
	  foreground = 1;
	  break;
	case 'n':
	  nicelevel = strtol (optarg, NULL, 10);
	  break;
	case 'V':
	  preload_log_level = strtol (optarg, NULL, 10);
	  break;
	case 'd':
	  logfile = NULL;
	  foreground = 1;
	  preload_log_level = 9;
	  break;
	case 'v':
	  version_func ();
	case 'H':
	case 'h':
	default:
	  help_func (i != 'h' && i != 'H', i == 'H');
	}
    }

  *argc -= optind;
  *argv += optind;
}




static void
version_func (void)
{
  printf ("%s\n\nCopyright (C) 2005,2006,2007,2008 Behdad Esfahbod.\n"
	  "This is free software; see the source for copying conditions.  There is NO\n"
	  "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
	  "\n"
	  "Written by Behdad Esfahbod <behdad@gnu.org>\n",
	  PACKAGE_STRING);
  exit (EXIT_SUCCESS);
}


static void
help_func (gboolean err, gboolean help2man)
{
  FILE *f = err ? stderr : stdout;
  const struct option *opt;
  const char **hlp, **dft;
  int max = 0;


  fprintf (f, "Usage: %s [OPTION]...\n"
	   "%s is an adaptive readahead daemon that prefetches files mapped by\n"
	   "applications from the disk to reduce application startup time.\n\n", PACKAGE, PACKAGE_NAME);

  for (opt = opts; opt->name; opt++)
    {
      int size = strlen (opt->name);
      if (size > max)
	max = size;
    }

  for (opt = opts, hlp = opts_help, dft = opts_default;
       opt->name; opt++, hlp++, dft++)
    {
      fprintf (f, "  -%c, --%-*s  %s\n",
	       opt->val, max, opt->name,
	       opt->val == 'h' && help2man ? help2man_str : *hlp);
      if (*dft)
	fprintf (f, "          %*s(default is %s)\n", max, "", *dft);
    }

  fprintf (f, "\nReport bugs to <%s>\n", PACKAGE_BUGREPORT);

  exit (err ? EXIT_FAILURE : EXIT_SUCCESS);
}

