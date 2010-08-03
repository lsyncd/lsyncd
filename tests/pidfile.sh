#!/bin/bash
set -e
CON="\E[47;34m"
COFF="\033[0m"

echo -e "$CON**************************************************$COFF"
echo -e "$CON** Checking if lsyncd writes a correct pidfile. **$COFF"
echo -e "$CON**************************************************$COFF"

WORKTARGET=$(mktemp -d)
PIDFILE=$(mktemp)
LOGFILE=$(mktemp)

./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" . "${WORKTARGET}"
sleep 1s
LSYNCPID=$(cat "${PIDFILE}")
if ! kill "${LSYNCPID}"; then
    echo "kill failed"
    exit 1
fi
sleep 1s
if kill "${LSYNCPID}"; then
    echo process still exists after kill
    exit 1
fi
rm "${PIDFILE}"
rm "${LOGFILE}"
rm -rf "${WORKTARGET}"

