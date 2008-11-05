#!/bin/bash
# test the case of directory being cp -r'ed and touched. lsyncd 1.0 doesn't handle this case well.

set -e

WORKSOURCE=$(mktemp -d)
WORKTARGET=$(mktemp -d)
PIDFILE=$(mktemp)
LOGFILE=$(mktemp)

# populate the filesystem.
mkdir -p "${WORKSOURCE}"/a/a
echo 'test' > "${WORKSOURCE}"/a/a/file

./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" "${WORKSOURCE}" "${WORKTARGET}"

# try to wait until lsyncd starts and rsyncs initial file, hope 1s is enough.
sleep 2s

# cp -r the directory, this sometimes succeeds, sometimes fails.
for A in 1 2 3 4 5 6 7 8 9 10; do
    cp -r "${WORKSOURCE}"/a "${WORKSOURCE}"/b${A}
    echo 'test2' > "${WORKSOURCE}"/b${A}/a/another
done

# mkdir path while lsyncd is running
mkdir -p "${WORKSOURCE}"/c/a
echo 'test3' > "${WORKSOURCE}"/c/a/file

# cp the dir while lsyncd is running.
# it's a race condition, do it 10 times.
for A in 1 2 3 4 5 6 7 8 9 10; do
    cp -r "${WORKSOURCE}"/c "${WORKSOURCE}"/d${A}
    echo 'test2' > "${WORKSOURCE}"/d${A}/a/another
done

# try to wait until lsyncd does the job.
sleep 2s

LSYNCPID=$(cat "${PIDFILE}")
if ! kill "${LSYNCPID}"; then
    cat "${LOGFILE}"
    diff -urN "${WORKSOURCE}" "${WORKTARGET}" || true
    echo "kill failed"
    exit 1
fi
sleep 1s


echo "log file contents"
cat "${LOGFILE}"
#this should be grep.

diff -urN "${WORKSOURCE}" "${WORKTARGET}"

rm "${PIDFILE}"
rm "${LOGFILE}"
rm -rf "${WORKTARGET}"
rm -rf "${WORKSOURCE}"
