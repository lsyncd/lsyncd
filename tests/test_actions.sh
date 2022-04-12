#!/usr/bin/env bash

if [ -z $1 ]; then
    echo "usage: test_actions [directory]"
    exit 1
fi

set -x

BLOBB1=$(realpath /tmp/src/../blobb1)

if [ ! -e $BLOBB1 ]; then
    echo "create outside blobb $BLOBB1"
    dd count=50 bs=1M if=/dev/urandom of=$BLOBB1
    echo done
fi

while true; do
    mkdir -p $1/testdir
    sleep 3
    touch $1/testfile
    sleep 2
    echo "blubb" >> $1/testfile
    sleep 2
    mv $1/testfile $1/testdir
    sleep 2
    ln $BLOBB1 $1/blubb1
    ln $BLOBB1 $1/testdir/blubb2
    ln $BLOBB1 $1/blubb3
    sleep 30
    rm $1/testdir/testfile
    sleep 2
    rm -rf $1/testdir
    sleep 1
    rm $1/blubb1
    rm $1/blubb3
    sleep 5
done
