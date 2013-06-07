#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#AC_PREREQ(2.60)
AC_INIT(lsyncd, 2.1.5, axkibe@gmail.com)
AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([lsyncd.c])
AC_CONFIG_HEADER([config.h])

###
# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

#AC_PATH_PROG([A2X], [a2x], [no])
#if test x${A2X} = xno ; then
#	AC_MSG_ERROR([Program 'a2x' (package asciidoc) is required])
#fi

###
# Checks for Lua

# Try versioned Lua 5.2 first
PKG_CHECK_MODULES([LUA52], [lua5.2],,[
	PKG_CHECK_MODULES([LUA52], [lua52],,[
		PKG_CHECK_MODULES([LUA52], [lua-5.2],,[:])
	])
])
AC_PATH_PROGS([LUA52], [lua5.2 lua52], [no])
AC_PATH_PROGS([LUAC52], [luac5.2 luac52], [no])

if test -z "${LUA52_PKG_ERRORS}" -a "${LUA52}" != no -a "${LUAC52}" != no ; then
	LUA_VERSION="5.2"
	LUA_CFLAGS="${LUA52_CFLAGS}"
	LUA_LIBS="${LUA52_LIBS}"
	LUA="${LUA52}"
	LUAC="${LUAC52}"
else
	# Fall back to versioned Lua 5.1
	PKG_CHECK_MODULES([LUA51], [lua5.1  >= 5.1.3],,[
		PKG_CHECK_MODULES([LUA51], [lua51   >= 5.1.3],,[
			PKG_CHECK_MODULES([LUA51], [lua-5.1 >= 5.1.3],,[:])
		])
	])
	AC_PATH_PROGS([LUA51], [lua5.1 lua51], [no])
	AC_PATH_PROGS([LUAC51], [luac5.1 luac51], [no])

	if test -z "${LUA51_PKG_ERRORS}" -a "${LUA51}" != no -a "${LUAC51}" != no ; then
		LUA_VERSION="5.1"
		LUA_CFLAGS="${LUA51_CFLAGS}"
		LUA_LIBS="${LUA51_LIBS}"
		LUA="${LUA51}"
		LUAC="${LUAC51}"
	else
		# Try any Lua now
		PKG_CHECK_MODULES([LUA], [lua >= 5.1.3],,[:])
		AC_PATH_PROG([LUA], [lua], [no])
		AC_PATH_PROG([LUAC], [luac], [no])

		if test -z "${LUA_PKG_ERRORS}" -a "${LUA}" != no -a "${LUAC}" != no ; then
			LUA_VERSION="(unknown version)"
		else
			AC_MSG_ERROR([Need a Lua toolchain with matching versions ('lua' library and 'lua' and 'luac' programs)])
		fi
	fi
fi

_LIBS="${LIBS}"
_CFLAGS="${CFLAGS}"
_CPPFLAGS="${CPPFLAGS}"
LIBS="-lm ${LIBS} ${LUA_LIBS}"
CFLAGS="${CFLAGS} ${LUA_CFLAGS}"
CPPFLAGS="${CPPFLAGS} ${LUA_CFLAGS}"

AC_MSG_CHECKING([whether Lua library was compiled with compat support])
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([
			#define LUA_COMPAT_ALL
			#include <lauxlib.h>
		],[luaL_register(0,0,0);])],
	[lua_compat_support=yes],
	[lua_compat_support=no]
)
AC_MSG_RESULT([${lua_compat_support}])

if test "x${lua_compat_support}" = xno ; then
	AC_MSG_ERROR([Lua library needs to be compiled with compat support])
fi

LIBS="${_LIBS}"
CFLAGS="${_CFLAGS}"
CPPFLAGS="${_CPPFLAGS}"
unset _LIBS _CFLAGS _CPPFLAGS

AX_SUBST_L([LUA_CFLAGS], [LUA_LIBS], [LUA], [LUAC])


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

AC_MSG_NOTICE([
Summary:
	Using Lua ${LUA_VERSION}
])
