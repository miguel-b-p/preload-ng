##                                               -*- Autoconf -*-
## Process this file with autoconf to produce a configure script.
## Use autoupdate to update this file for newer versions of autoconf.
## Use autoscan to check if you need to add something to this file.
##############################################################################

AC_PREREQ(2.60)
AC_INIT([preload], [0.6.4], [http://preload.sf.net/bug])
AC_CONFIG_SRCDIR(src/preload.c)
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE(foreign)
AC_USE_SYSTEM_EXTENSIONS

PACKAGE_SUMMARY="Adaptive readahead daemon"
AC_SUBST(PACKAGE_SUMMARY)
DOWNLOAD_URL_BASE="http://prdownloads.sourceforge.net/${PACKAGE_NAME}"
AC_SUBST(DOWNLOAD_URL_BASE)
SITE_URL="http://${PACKAGE_NAME}.sf.net/"
AC_SUBST(SITE_URL)
MAILING_LIST="${PACKAGE_NAME}-devel@lists.sf.net"
AC_SUBST(MAILING_LIST)

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for header files.

AC_CHECK_HEADERS([stdlib.h string.h strings.h inttypes.h sys/times.h])

# Checks for typedefs and structures.
AC_C_CONST

# Checks for compiler characteristics.
changequote(,)dnl
if test "x$GCC" = "xyes"; then
  CFLAGS="$CFLAGS -Wall -ansi "
fi
changequote([,])dnl

# Checks for library functions and headers.
AC_CHECK_FUNCS(readahead,,[echo "*** readahead(2) system call required but not found.";
			   echo "*** get yourself a recent Linux and glibc";
			   echo "*** Exiting";
			   exit 1;])



AC_CHECK_HEADERS([linux/fs.h])

# Checks for libraries.
PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.14)

# Directories we need.
pkgdocdir='${datadir}/doc/'${PACKAGE_NAME}-${PACKAGE_VERSION}
initddir='${sysconfdir}/rc.d/init.d'
sysconfigdir='${sysconfdir}/sysconfig'
logrotatedir='${sysconfdir}/logrotate.d'
logdir='${localstatedir}/log'
subsysdir='${localstatedir}/lock/subsys'
pkglocalstatedir='${localstatedir}/lib/'${PACKAGE_NAME}
AC_SUBST(pkgdocdir)
AC_SUBST(initddir)
AC_SUBST(sysconfigdir)
AC_SUBST(logrotatedir)
AC_SUBST(logdir)
AC_SUBST(subsysdir)
AC_SUBST(pkglocalstatedir)

# Generate output
AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 src/Makefile])
AC_OUTPUT
