#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#AC_PREREQ(2.60)
AC_INIT(lsyncd, 2.0.7, axkibe@gmail.com)
AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([lsyncd.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_PATH_PROG([A2X], [a2x], [no])
if test x${A2X} = xno ; then
	AC_MSG_ERROR([Program 'a2x' (package asciidoc) is required])
fi

###
# Checks for lua
PKG_CHECK_MODULES([LUA], [lua5.1  >= 5.1.3],,[
PKG_CHECK_MODULES([LUA], [lua51   >= 5.1.3],,[
PKG_CHECK_MODULES([LUA], [lua-5.1 >= 5.1.3],,[
PKG_CHECK_MODULES([LUA], [lua >= 5.1.3])
])
])
])

AC_PATH_PROGS([LUA], [lua5.1 lua51 lua], [no])
if test x${LUA} = xno ; then
	AC_MSG_ERROR([Program 'lua' is required])
fi

AC_PATH_PROGS([LUAC], [luac5.1 luac51 luac], [no])
if test x${LUAC} = xno ; then
	AC_MSG_ERROR([Program 'luac' is required])
fi

###
# Checks for header files.
AC_CHECK_HEADERS([sys/inotify.h])

###
# --without-inotify option
AC_ARG_WITH([inotify],
[  --without-inotify       Do not use Linux inotify event interface. On by default.],
[],[with_inotify=yes])
if test "x${with_inotify}" == xyes; then
	echo "compiling with inotify"
	AC_DEFINE(LSYNCD_WITH_INOTIFY,,"descr")
else
	echo "compiling without inotify"
fi
AM_CONDITIONAL([INOTIFY], [test x${with_inotify} != xno])

###
# --with-fsevents
# disabled per default, experimental, works only with OS X 10.5/10.6
AC_ARG_WITH([fsevents],
[  --with-fsevents         Uses MacOS (10.5) /dev/fsevents. EXPERIMENTAL!
                          Off by default.])
if test "x${with_fsevents}" == xyes; then
	echo "compiling with fsevents. WARNING experimental!"
	AC_DEFINE(LSYNCD_WITH_FSEVENTS,,"descr")
fi
AM_CONDITIONAL([FSEVENTS],
	[test x${with_fsevents} != x -a xno${with_fsevents} != xno])

# Checks for typedefs, structures, and compiler characteristics.
# Checks for library functions.
AC_CONFIG_FILES([Makefile])
AC_OUTPUT


