#!/usr/bin/env bash

set -ex

SRC=`pwd`
BUILD_FOLDER=`mktemp -d`
echo "Build folder: $BUILD_FOLDER"
cd $BUILD_FOLDER
cmake $SRC
make VERBOSE=1
make tests
rm -rf $BUILD_FOLDER