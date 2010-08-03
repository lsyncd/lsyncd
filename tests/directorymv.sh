#!/bin/bash
set -e
CON="\E[47;34m"
COFF="\033[0m"

echo -e "$CON************************************************************$COFF"
echo -e "$CON** Testing the case of directory being moved and removed. **$COFF"
echo -e "$CON************************************************************$COFF"

WORKSOURCE=$(mktemp -d)
WORKTARGET=$(mktemp -d)
PIDFILE=$(mktemp)
LOGFILE=$(mktemp)

echo $WORKSOURCE
echo $WORKTARGET
echo $PIDFILE

echo -e "$CON* populating the filesystem.$COFF"
mkdir "${WORKSOURCE}"/a
mkdir "${WORKSOURCE}"/b
touch "${WORKSOURCE}"/a/f
touch "${WORKSOURCE}"/b/g

echo -e "$CON* starting lsyncd.$COFF"
./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" "${WORKSOURCE}" "${WORKTARGET}"
LSYNCPID=$(cat "${PIDFILE}")

echo -e "$CON* waiting for lsyncd to start.$COFF"
sleep 4s

# move a file
echo -e "$CON* moving a directory$COFF"
mv "${WORKSOURCE}"/a "${WORKSOURCE}"/c

echo -e "$CON* creating a file there$COFF"
touch "${WORKSOURCE}"/c/h

echo -e "$CON* and deleting a directory$COFF"
rm -r "${WORKSOURCE}"/b

echo -e "$CON* waiting for lsyncd to do the job.$COFF"
sleep 20s

echo -e "$CON* killing daemon$COFF"
if ! kill "${LSYNCPID}"; then
    cat "${LOGFILE}"
    diff -ur "${WORKSOURCE}" "${WORKTARGET}" || true
    echo "kill failed"
    exit 1
fi
sleep 1s

echo -e "$CON* log file contents$COFF"
cat "${LOGFILE}"

echo -e "$CON* differences$COFF"
diff -ur "${WORKSOURCE}" "${WORKTARGET}"

rm "${PIDFILE}"
rm "${LOGFILE}"
rm -rf "${WORKTARGET}"
rm -rf "${WORKSOURCE}"
