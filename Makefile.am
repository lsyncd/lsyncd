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

EXTRA_DIST = config.h.in inotify-nosys.h lsyncd.conf.xml \
	doc/manpage.lsyncd.xml doc/manpage.lsyncd.conf.xml \
	doc/lsyncd.1 doc/lsyncd.conf.xml.5

nodist_man1_MANS = doc/lsyncd.1 doc/lsyncd.conf.xml.5

check-syntax:
	$(CC) -c -O2 -Wall $(CHK_SOURCES)  -o/dev/null 

DB2MAN = /usr/share/sgml/docbook/stylesheet/xsl/nwalsh/manpages/docbook.xsl
XP     = xsltproc -''-nonet \
          -''-param man.charmap.use.subset "0" \
          -''-param make.year.ranges "1" \
          -''-param make.single.year.ranges "1"

doc/lsyncd.1: doc/manpage.lsyncd.xml
	$(XP) -o $@ $(DB2MAN) $<

doc/lsyncd.conf.xml.5: doc/manpage.lsyncd.conf.xml
	$(XP) -o $@ $(DB2MAN) $< 

AM_CFLAGS=-Wall 
AM_LDFLAGS=
if XML_CONFIG
DEFS+=-DXML_CONFIG
AM_CFLAGS += `xml2-config --cflags`
AM_LDFLAGS += `xml2-config --libs`
endif

