#!/usr/bin/env bash

set -ex

function cleanup() {
        echo "** abort. cleanup ssh server"
        cd `dirname $BASH_SOURCE`/..
        # CLEANUP=`dirname $BASH_SOURCE`/../teardown.lua
        lua tests/teardown.lua
}

trap cleanup INT EXIT

SRC=`pwd`
BUILD_FOLDER=`mktemp -d`
echo "Build folder: $BUILD_FOLDER"
cd $BUILD_FOLDER
cmake $SRC
make VERBOSE=1
make run-tests
rm -rf $BUILD_FOLDER