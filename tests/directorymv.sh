#!/bin/bash
# test the case of directory being mv'ed and rm'ed. lsyncd 1.0 didn't handle this case well.

set -e

WORKSOURCE=$(mktemp -d)
WORKTARGET=$(mktemp -d)
PIDFILE=$(mktemp)
LOGFILE=$(mktemp)

echo $WORKSOURCE
echo $WORKTARGET
echo $PIDFILE

# populate the filesystem.
mkdir "${WORKSOURCE}"/a
mkdir "${WORKSOURCE}"/b
touch "${WORKSOURCE}"/a/f
touch "${WORKSOURCE}"/b/g

echo ./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" "${WORKSOURCE}" "${WORKTARGET}"
./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" "${WORKSOURCE}" "${WORKTARGET}"
LSYNCPID=$(cat "${PIDFILE}")

# try to wait until lsyncd starts and rsyncs initial file, hope 2s is enough.
sleep 2s

# move a file
echo "moving a directory"
mv "${WORKSOURCE}"/a "${WORKSOURCE}"/c
echo "create a file there"
touch "${WORKSOURCE}"/c/h

echo "and delete a directory"
#lsyncd 1.0 dies here
rm -r "${WORKSOURCE}"/b

echo "wait for events to trigger"
# try to wait until lsyncd does the job.
sleep 10s
echo "killing daemon"

if ! kill "${LSYNCPID}"; then
    cat "${LOGFILE}"
    diff -ur "${WORKSOURCE}" "${WORKTARGET}" || true
    echo "kill failed"
    exit 1
fi
sleep 1s


echo "log file contents"
cat "${LOGFILE}"
#this should be grep.

diff -ur "${WORKSOURCE}" "${WORKTARGET}"

#rm "${PIDFILE}"
#rm "${LOGFILE}"
#rm -rf "${WORKTARGET}"
#rm -rf "${WORKSOURCE}"
