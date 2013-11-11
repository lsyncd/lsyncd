#/bin/bash

set -e

srcdir=$(mktemp -d)
tgtdir=$(mktemp -d)
lsyncdpidfile=$(mktemp)
trap 'cleanup' INT TERM EXIT
cleanup() {
    [ -d "$srcdir" ] && rm -rf "$srcdir"
    [ -d "$tgtdir" ] && rm -rf "$tgtdir"
}

delay=5
cat <<EOF>lsyncd_test.config
settings {
        logfile         = "/tmp/lsyncd.log",
        log             = "all",
        statusFile      = "/tmp/lsyncd.status",
        statusIntervall = 1,
        delay           = $delay,
        pidfile         = "$lsyncdpidfile",
        nodaemon        = true,
}
sync {
	default.rsync,
	source = "$srcdir/",
	target = "$tgtdir/"
}
EOF
./lsyncd lsyncd_test.config &
lsyncdpid=$!

# enough time to setup inotify watch
# and run initial rsync
sleep 2

for N in $(seq 1 100) ; do
	# touch $srcdir/$N
	mkdir $srcdir/$N
	for M in $(seq 1 100); do
	 	touch $srcdir/$N/$M
	done
done

#sleep 60

#cat /tmp/lsyncd.status 

## Uncomment this delay for an artificial success
#sleep $delay

#tail -n1 /tmp/lsyncd.log
sleep 0.1
#lsyncdpid=$(<$lsyncdpidfile)
# SIGTERM 15
kill -s 15 $lsyncdpid

# trigger some events after SIGTERM
# to ensure they are properly ignored
(for N in $(seq 101 110) ; do
	touch $srcdir/$N
done) &

# catch lsyncd
set +e
wait $lsyncdpid 
lsyncdexitcode=$?
set -e
if [ 143 -ne $lsyncdexitcode ]; then
	echo "lsyncd abnormal exit code $lsyncdexitcode"
	exit 1
fi

count=$(find $tgtdir -type f | wc -l)
expected=10000
if [ $count -eq $expected ]; then
	echo Correct
	exit 
else
	echo "Found $count - expected $expected"
	sleep 1
	count=$(find $tgtdir -type f | wc -l)
	echo recount $count
	exit 1
fi

