#!/bin/bash
set -e
C1="\E[47;34m"
C0="\033[0m"

echo -e "$C1****************************************************************$C0"
echo -e "$C1 Testing layer 4 default rsync with simulated data activity     $C0"
echo -e "$C1****************************************************************$C0"
echo
#root tmp dir
R=$(mktemp -d)
#source dir
S=$R/source
#target dir
T=$R/target
echo -e "$C1* using root dir for test $R$C0"
echo -e "$C1* populating the source$C0"
mkdir -p "$S"/d1/d11
echo 'test' > "$S"/d1/d11/f1
echo -e "$C1* starting lsyncd$C0"
./lsyncd --logfile "${LOGFILE}" --pidfile "${PIDFILE}" --verbose --no-daemon "${WORKSOURCE}" "${WORKTARGET}"&
echo -e "$C1* waiting for lsyncd to start$C0"
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

echo -e "$CON* waiting for lsyncd to do the job.$COFF"
sleep 10s

echo -e "$CON* killing lsyncd$COFF"
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

echo -e "$CON* differences$COFF"
diff -urN "${WORKSOURCE}" "${WORKTARGET}"

rm "${PIDFILE}"
rm "${LOGFILE}"
rm -rf "${WORKTARGET}"
rm -rf "${WORKSOURCE}"

