#ifndef CONF_H
#define CONF_H

/* units */

#define bytes		   1
#define kilobytes	1024

#define seconds		   1
#define minutes		  60
#define hours		3600

#define signed_integer_percent	   1

#define processes	   1


typedef struct _preload_conf_t
{
  /* conf values.  see preload.conf for a description of these */
  /* all time and size values here are in seconds and bytes,
   * unlike in the config file */

  struct _conf_model {
    int cycle;
    gboolean usecorrelation;

    int minsize;

    /* memory usage adjustment (signed percentages) */
    int memtotal;
    int memfree;
    int memcached;
    int membuffers;  /* percentage of buffers to consider reclaimable (default: 50%) */
  } model;

  struct _conf_system {
    gboolean doscan;
    gboolean dopredict;
    int autosave;

    char **mapprefix;
    char **exeprefix;

    int maxprocs;
    enum {
      SORT_NONE  = 0,
      SORT_PATH  = 1,
      SORT_INODE = 2,
      SORT_BLOCK = 3
    } sortstrategy;
    
    char *prediction_algorithm;  /* "Markov" or "VOMM" */
  } system;

} preload_conf_t;

extern preload_conf_t conf[1];


void preload_conf_load (const char *conffile, gboolean fail);
void preload_conf_dump_log (void);

/* Helper to check if VOMM algorithm is selected (handles NULL and quoted values) */
gboolean preload_is_vomm_algorithm (void);

#endif
