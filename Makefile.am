## Makefile.am -- Process this file with automake to produce Makefile.in
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.c 
TESTS = tests/help.sh \
	tests/directorymv.sh \
	tests/directorycpr.sh \
	tests/pidfile.sh \
	tests/version.sh \
	tests/wrong-logfile.sh \
	tests/wrong-rsync.sh

datarootdir = @datarootdir@

EXTRA_DIST = config.h.in inotify-nosys.h manpage.xml lsyncd.conf.xml

nodist_man1_MANS = lsyncd.1 lsyncd.conf.xml.5

check-syntax:
	$(CC) -c -O2 -Wall $(CHK_SOURCES)  -o/dev/null 

DB2MAN = /usr/share/sgml/docbook/stylesheet/xsl/nwalsh/manpages/docbook.xsl
XP     = xsltproc -''-nonet \
          -''-param man.charmap.use.subset "0" \
          -''-param make.year.ranges "1" \
          -''-param make.single.year.ranges "1"

lsyncd.1: manpage.lsyncd.xml
	$(XP) $(DB2MAN) $<

lsyncd.conf.xml.5: manpage.lsyncd.conf.xml
	$(XP) $(DB2MAN) $<

AM_CFLAGS=-Wall 
AM_LIBS=
if XML_CONFIG
DEFS+=-DXML_CONFIG
AM_CFLAGS += `xml2-config --cflags`
AM_CFLAGS += `xml2-config --libs`
endif

