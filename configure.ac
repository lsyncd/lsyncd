#                                               -*- Autoconf -*-
echo XXXXXXXXX
# Process this file with autoconf to produce a configure script.
#AC_PREREQ(2.60)
AC_INIT(lsyncd, 2.0beta3, axkibe@gmail.com)
AC_CONFIG_SRCDIR([lsyncd.c])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
# Checks for libraries.
PKG_CHECK_MODULES(LIBLUA, lua5.1)
# Checks for header files.
AC_CHECK_HEADERS([sys/inotify.h])
AC_CHECK_HEADERS([sys/fanotify.h])

###
# --with-runner option
AC_ARG_WITH([runner],
[  --with-runner=<dir>     Specify directory where lsyncds part written in Lua will be placed.
                          If missing it will be compiled into the binary])
if test "x${with_runner}" != x; then
	AC_DEFINE_UNQUOTED(LSYNCD_DEFAULT_RUNNER_FILE, "${with_runner}/lsyncd.lua", "descr")
	AC_SUBST(RUNNER_DIR, "${with_runner}")
fi
AM_CONDITIONAL([RUNNER],  [test x${with_runner} != x])

###
# --without-inotify option
AC_ARG_WITH([inotify],
[  --without-inotify       Do not use Linux inotify event interface. On by default.])
if test "x${with_inotify}" == xno; then
	echo "compiling without inotify"
else
	echo "compiling with inotify"
	AC_DEFINE(LSYNCD_WITH_INOTIFY,,"descr")
fi
AM_CONDITIONAL([INOTIFY], [test x${with_inotify} != xno])

###
# --without-inotify option
AC_ARG_WITH([fanotify],
[  --with-fanotify       Uses Linux new fanotify event interface. EXPERIMENTAL!
                        Off by default.])
if test "x${with_fanotify}" == xno; then
	echo "compiling without fanotify"
else
	echo "compiling with fanotify - WARNING experimental!"
	AC_DEFINE(LSYNCD_WITH_FANOTIFY,,"descr")
fi
AM_CONDITIONAL([FANOTIFY], 
	[test x${with_fanotify} != x -a x${with_fanotify} != xno])

###
# --with-fsevents 
AC_ARG_WITH([fsevents],
[  --with-fsevents         Uses MacOS (10.5) /dev/fsevents. EXPERIMENTAL!
                          Off by default.])
if test "x${with_fsevents}" != x; then
	echo "compiling with fsevents. WARNING experimental!"
	AC_DEFINE(LSYNCD_WITH_FSEVENTS,,"descr")
else
	echo "compiling without fsevents"
fi
AM_CONDITIONAL([FSEVENTS], 
	[test x${with_fsevents} != x -a xno${with_fsevents} != xno])

# Checks for typedefs, structures, and compiler characteristics.
# Checks for library functions.
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
