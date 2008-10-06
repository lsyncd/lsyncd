## Makefile.am -- Process this file with automake to produce Makefile.in
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.c 
TESTS = tests/help.sh \
	tests/version.sh \
	tests/wrong-logfile.sh \
	tests/wrong-rsync.sh

datarootdir = @datarootdir@

EXTRA_DIST = config.h.in inotify-nosys.h
