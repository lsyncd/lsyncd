#!/bin/bash
# copyright 2008 Junichi Uekawa <dancer@debian.org>
# licensed under GPLv2 or later, see the file ../COPYING for details.

# make sure that program exits with exit code of -1 when rsync path is
# wrong.

WORKTARGET=$(mktemp -d)
./lsyncd --no-daemon --rsync-binary /wrong/path/to/rsync . "${WORKTARGET}"
if [[ $? = 3 ]]; then
    rmdir "${WORKTARGET}"
    exit 0;
else
    exit 1;
fi

