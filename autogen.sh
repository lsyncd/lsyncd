#!/bin/bash
# simplistic script to run after checkout

# In case of explicit having a 1.7 version use that (for systems having multiple automake versions installed)
ACLOCAL=`which aclocal-1.7 || echo aclocal`
AUTOMAKE=`which automake-1.7 || echo automake`

$ACLOCAL && \
autoheader && \
autoconf && \
$AUTOMAKE -a -c
