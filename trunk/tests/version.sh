#!/bin/bash
# copyright 2008 Junichi Uekawa <dancer@debian.org>
# licensed under GPLv2 or later, see the file ../COPYING for details.

set -e 
CON="\E[47;34m"
COFF="\033[0m"

echo -e "$CON******************************************************************************$COFF"
echo -e "$CON* Testing that --version outputs some kind of version message and exit code 0.$COFF"
echo -e "$CON******************************************************************************$COFF"

set -o pipefail
./lsyncd --version | grep '^Version: '

