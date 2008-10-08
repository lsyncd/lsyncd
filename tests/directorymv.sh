#!/bin/bash
# test the case of directory being mv'ed and rm'ed. lsyncd 1.0 didn't handle this case well.

set -e

WORKSOURCE=$(mktemp -d)
WORKTARGET=$(mktemp -d)
PIDFILE=$(mktemp)
LOGFILE=$(mktemp)


# populate the filesystem.
mkdir "${WORKSOURCE}"/a
mkdir "${WORKSOURCE}"/b
touch "${WORKSOURCE}"/a/f
touch "${WORKSOURCE}"/b/g

./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" "${WORKSOURCE}" "${WORKTARGET}"

# try to wait until lsyncd starts and rsyncs initial file, hope 1s is enough.
sleep 2s

# move a file
mv "${WORKSOURCE}"/a "${WORKSOURCE}"/c
touch "${WORKSOURCE}"/c/h

#lsyncd 1.0 dies here
rm -r "${WORKSOURCE}"/b

# try to wait until lsyncd does the job.
sleep 2s

LSYNCPID=$(cat "${PIDFILE}")
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

rm "${PIDFILE}"
rm "${LOGFILE}"
rm -rf "${WORKTARGET}"
rm -rf "${WORKSOURCE}"
