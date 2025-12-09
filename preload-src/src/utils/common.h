#ifndef PRELOAD_COMMON_H
#define PRELOAD_COMMON_H

#if HAVE_CONFIG_H
# include <config.h>
#endif

/* TODO: make includes (in C files too) conditional */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif


#include <glib.h>


#ifdef HAVE_STRINGIZE
# define STRINGIZE2(x)	#x
#else
# define STRINGIZE2(x)	"?"
# error Your C preprocessor does not support stringize operator.
#endif
#define STRINGIZE(x)	STRINGIZE2(x)


#if 0
# define GSet			GSList
# define g_set_new()		NULL
# define g_set_add(s,v)		(s = g_slist_prepend (s,v))
# define g_set_remove(s,v)	(s = g_slist_remove (s,v))
# define g_set_free(s)		g_slist_free (s)
# define g_set_size(s)		g_slist_length (s)
# define g_set_foreach		g_slist_foreach
#else
# define GSet			GPtrArray
# define g_set_new()		g_ptr_array_new ()
# define g_set_add(s,v)		g_ptr_array_add (s, v)
# define g_set_remove(s,v)	g_ptr_array_remove_fast (s, v)
# define g_set_free(s)		g_ptr_array_free (s, TRUE)
# define g_set_size(s)		((s)->len)
# define g_set_foreach		g_ptr_array_foreach
#endif


#define FILELEN 512
#define FILELENSTR "511"

#endif
