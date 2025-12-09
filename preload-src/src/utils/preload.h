#ifndef PRELOAD_H
#define PRELOAD_H

#include <log.h>

#define DEFAULT_CONFFILE	SYSCONFDIR "/" PACKAGE ".conf"
#define DEFAULT_STATEFILE	PKGLOCALSTATEDIR "/" PACKAGE ".state"
#define DEFAULT_LOGFILE		LOGDIR "/" PACKAGE ".log"
#define DEFAULT_LOGLEVEL	4
#define DEFAULT_NICELEVEL	15

extern const char *conffile;
extern const char *statefile;
extern const char *logfile;
extern int foreground;
extern int nicelevel;

#endif
