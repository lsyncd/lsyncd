#!/bin/bash
# copyright 2008 Junichi Uekawa <dancer@debian.org>
# licensed under GPLv2 or later, see the file ../COPYING for details.

# test that --help outputs help message and exit code of 0
set -e 
set -o pipefail

# assume that USAGE being in output is good enough.

./lsyncd --help | grep '^USAGE:' 
