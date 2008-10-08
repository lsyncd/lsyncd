## Makefile.am -- Process this file with automake to produce Makefile.in
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.c 
TESTS = tests/help.sh \
	tests/pidfile.sh \
	tests/version.sh \
	tests/wrong-logfile.sh \
	tests/wrong-rsync.sh

datarootdir = @datarootdir@

EXTRA_DIST = config.h.in inotify-nosys.h manpage.xml

nodist_man1_MANS = lsyncd.1

check-syntax:
	$(CC) -c -O2 -Wall $(CHK_SOURCES)  -o/dev/null 

lsyncd.1: manpage.xml
	xsltproc -''-nonet \
          -''-param man.charmap.use.subset "0" \
          -''-param make.year.ranges "1" \
          -''-param make.single.year.ranges "1" \
          /usr/share/xml/docbook/stylesheet/nwalsh/manpages/docbook.xsl \
          manpage.xml
