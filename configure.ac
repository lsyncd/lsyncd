#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#AC_PREREQ(2.60)
AC_INIT(lsyncd, 2.0beta2, axkibe@gmail.com)
AC_CONFIG_SRCDIR([lsyncd.c],[lsyncd.lua],[inotify.c])
AC_CONFIG_HEADER([config.h],[lsyncd.h])
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
# Checks for libraries.
PKG_CHECK_MODULES(LIBLUA, lua5.1)
# Checks for header files.
AC_CHECK_HEADERS([sys/inotify.h])

AC_ARG_WITH([runner],
[  --with-runner=<dir>    Specify directory where lsyncds part written in Lua will be placed.
                          If missing it will be compiled into the binary)])
if test "x${with_runner}" != x; then
AC_DEFINE_UNQUOTED(LSYNCD_DEFAULT_RUNNER_FILE, "${with_runner}/lsyncd.lua", "descr")
AC_SUBST(RUNNER_DIR, "${with_runner}")
fi
AM_CONDITIONAL([RUNNER], [test x${with_runner} != x])

# Checks for typedefs, structures, and compiler characteristics.
# Checks for library functions.
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
