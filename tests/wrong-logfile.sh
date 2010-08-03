#!/bin/bash
# copyright 2008 Junichi Uekawa <dancer@debian.org>
# licensed under GPLv2 or later, see the file ../COPYING for details.
set -e 
CON="\E[47;34m"
COFF="\033[0m"

echo -e "$CON*****************************************************************$COFF"
echo -e "$CON* Testing that a wrong logfile spec. gives a reasonable error. **$COFF"
echo -e "$CON*****************************************************************$COFF"

WORKTARGET=$(mktemp -d)
if [[ $( ./lsyncd --logfile /nonexisting/path/name . "${WORKTARGET}" 2>&1 ) == "cannot open logfile [/nonexisting/path/name]!" ]]; then
    rmdir "${WORKTARGET}"
    exit 0;
else
    rmdir "${WORKTARGET}"
    exit 1;
fi

