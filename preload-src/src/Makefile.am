LDADD =		\
		-lm	\
		$(GLIB_LIBS)

AM_CPPFLAGS =	\
		$(GLIB_CFLAGS)	\
		-DSYSCONFDIR='"${sysconfdir}"'	\
		-DLOGDIR='"${logdir}"'	\
		-DPKGLOCALSTATEDIR='"${pkglocalstatedir}"'

preload.o cmdline.o preload.8: Makefile

sbin_PROGRAMS = preload

preload_SOURCES =	\
		cmdline.c	\
		cmdline.h	\
		common.h	\
		conf.c	\
		conf.h	\
		confkeys.h	\
		vomm.c	\
		vomm.h	\
		log.c	\
		log.h	\
		madvise_utils.c	\
		madvise_utils.h	\
		model_utils.c	\
		model_utils.h	\
		spy.c	\
		spy.h	\
		preload.c	\
		preload.h	\
		proc.c	\
		proc.h	\
		prophet.c	\
		prophet.h	\
		readahead.c	\
		readahead.h	\
		state.c	\
		state.h	\
		time_utils.c	\
		time_utils.h

EXTRA_DIST =	\
		preload.8.i	\
		preload.conf.in	\
		gen.preload.conf.sh	\
		runtests.sh

TESTS = runtests.sh

check: preload.conf

DISTCLEANFILES = preload.conf preload.state preload.log $(MAINTAINERCLEANFILES)
nodist_sysconf_DATA = preload.conf
preload.conf: gen.preload.conf.sh preload.conf.in confkeys.h
	$(SHELL) $^ > $@.tmp && mv $@.tmp $@


MAINTAINERCLEANFILES = preload.8

dist_man_MANS = preload.8

$(srcdir)/preload.8: cmdline.c ../configure.ac preload.8.i
	$(MAKE) $(AM_MAKEFLAGS) preload
	@echo "Creating $@ using help2man(1)"
	@$(top_builddir)/missing --run \
	   help2man --no-info --section=8 --include=preload.8.i \
	   	    --help-option="-H" --output="$@.tmp" \
		    --name 'Adaptive readahead daemon' ./preload \
	 && mv "$@.tmp" "$@" \
	  || ($(RM) "$@"; \
	      echo Failed to update preload.8, the man page may be outdated >&2; \
	      (test -f "$@" || echo help2man is required to generate this file. >> "$@"));

install-data-hook:
	@cd "$(DESTDIR)$(man8dir)" && gzip -c preload.8 > preload.8.gz.tmp && mv preload.8.gz.tmp preload.8.gz && $(RM) preload.8

uninstall-hook:
	$(RM) "$(DESTDIR)$(man8dir)/preload.8.gz"

######################################################################

MAINTAINERCLEANFILES += preload.conf.debug

preload.conf.debug:
	$(MAKE) $(AM_MAKEFLAGS) preload.conf
	cp preload.conf preload.conf.debug

RUNPREQ = preload preload.conf.debug
RUNCMD = ./preload -c preload.conf.debug -s preload.state -d $(ARGS)

run: $(RUNPREQ)
	$(RUNCMD)

run-valgrind: $(RUNPREQ)
	valgrind --tool=memcheck --leak-check=yes $(RUNCMD)

run-gdb: $(RUNPREQ)
	gdb --args $(RUNCMD)

errors:
	clear
	@$(MAKE) $(AM_MAKEFLAGS) clean all > /dev/null
