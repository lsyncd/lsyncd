#!/bin/bash
# copyright 2008 Junichi Uekawa <dancer@debian.org>
# licensed under GPLv2 or later, see the file ../COPYING for details.

# test that --version outputs some kind of version message and exit code of 0
set -e 
set -o pipefail

./lsyncd --version | grep '^Version: '


