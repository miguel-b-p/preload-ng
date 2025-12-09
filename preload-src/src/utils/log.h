#ifndef LOG_H
#define LOG_H

extern int preload_log_level;
void preload_log_init (const char *logfile);
void preload_log_reopen (const char *logfile);

#define preload_is_debugging() (G_LOG_LEVEL_DEBUG <= G_LOG_LEVEL_ERROR << preload_log_level)

#endif
