#!/bin/bash
# copyright 2008 Junichi Uekawa <dancer@debian.org>
# licensed under GPLv2 or later, see the file ../COPYING for details.

#set -e  <- explicitly not!
CON="\E[47;34m"
COFF="\033[0m"

echo -e "$CON******************************************************************$COFF"
echo -e "$CON* Testing if lsyncd exits with -1 when the rsync path is wrong. **$COFF"
echo -e "$CON******************************************************************$COFF"

WORKTARGET=$(mktemp -d)
./lsyncd --no-daemon --binary /wrong/path/to/rsync . "${WORKTARGET}"
if [[ $? = 3 ]]; then
    rmdir "${WORKTARGET}"
    exit 0;
else
    exit 1;
fi
