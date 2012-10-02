
# ax_subst_l.m4 - Substitute every var in the given comma seperated list -*-Autoconf-*-
#
#   Copyright (C) 2012 Dennis Schridde
#
# This file is free software; the authors give
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.

# serial 1

# Substitute every var in the given comma seperated list
AC_DEFUN([AX_SUBST_L],[
    m4_foreach([__var__], [$@], [AC_SUBST(__var__)])
])
