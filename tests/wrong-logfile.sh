#!/bin/bash
# copyright 2008 Junichi Uekawa <dancer@debian.org>
# licensed under GPLv2 or later, see the file ../COPYING for details.

# make sure wrong logfile specification gives a reasonable error
# message

WORKTARGET=$(mktemp -d)
if [[ $( ./lsyncd --logfile /nonexisting/path/name . "${WORKTARGET}" 2>&1 ) =~ "cannot open logfile [/nonexisting/path/name]!" ]]; then
    rmdir "${WORKTARGET}"
    exit 0;
else
    rmdir "${WORKTARGET}"
    exit 1;
fi

