#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#AC_PREREQ(2.60)
AC_INIT(lsyncd, 2.0b1, axkibe@gmail.com)
AC_CONFIG_SRCDIR([lsyncd.c])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE(lsyncd, main)
# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
# Checks for libraries.
PKG_CHECK_MODULES(LIBLUA, lua5.1)
# Checks for header files.
# Checks for typedefs, structures, and compiler characteristics.
# Checks for library functions.
AC_CHECK_HEADERS([sys/inotify.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
