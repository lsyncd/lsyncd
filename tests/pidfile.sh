#!/bin/bash
set -e

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

