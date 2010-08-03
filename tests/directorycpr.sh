#!/bin/bash
set -e
CON="\E[47;34m"
COFF="\033[0m"

echo -e "$CON***************************************************************$COFF"
echo -e "$CON** Testing the case of directory being cp -r'ed and touched. **$COFF" 
echo -e "$CON** With default delay                                        **$COFF"
echo -e "$CON***************************************************************$COFF"

WORKSOURCE=$(mktemp -d)
WORKTARGET=$(mktemp -d)
PIDFILE=$(mktemp)
LOGFILE=$(mktemp)

echo -e "$CON* populating the filesystem$COFF"
mkdir -p "${WORKSOURCE}"/a/a
echo 'test' > "${WORKSOURCE}"/a/a/file

echo -e "$CON* starting lsyncd$COFF"
./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" --no-daemon "${WORKSOURCE}" "${WORKTARGET}"&

echo -e "$CON* waiting for lsyncd to start$COFF"
sleep 4s

# cp -r the directory
echo -e "$CON* making a lot of data$COFF"
for A in 1 2 3 4 5 6 7 8 9 10; do
    cp -r "${WORKSOURCE}"/a "${WORKSOURCE}"/b${A}
    echo 'test2' > "${WORKSOURCE}"/b${A}/a/another
done
mkdir -p "${WORKSOURCE}"/c/a
echo 'test3' > "${WORKSOURCE}"/c/a/file
for A in 1 2 3 4 5 6 7 8 9 10; do
    cp -r "${WORKSOURCE}"/c "${WORKSOURCE}"/d${A}
    echo 'test2' > "${WORKSOURCE}"/d${A}/a/another
done

echo -e "$CON*waiting until lsyncd does the job.$COFF"
sleep 20s

echo -e "$CON*killing lsyncd$COFF"
LSYNCPID=$(cat "${PIDFILE}")
if ! kill "${LSYNCPID}"; then
    cat "${LOGFILE}"
    diff -urN "${WORKSOURCE}" "${WORKTARGET}" || true
    echo "kill failed"
    exit 1
fi
sleep 1s

#echo "log file contents"
#cat "${LOGFILE}"
##this should be grep.

echo -e "$CON*differences$COFF"
diff -urN "${WORKSOURCE}" "${WORKTARGET}"

rm "${PIDFILE}"
rm "${LOGFILE}"
rm -rf "${WORKTARGET}"
rm -rf "${WORKSOURCE}"
