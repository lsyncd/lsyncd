AUTOMAKE_OPTIONS = foreign
CFLAGS += -Wall $(LUA_CFLAGS) 
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.h lsyncd.c lsyncd.lua
if INOTIFY
lsyncd_SOURCES += inotify.c
endif

#if FSEVENTS
#lsyncd_SOURCES += fsevents.c
#endif

lsyncd_LDADD = $(LUA_LIBS)
exampledir = $(docdir)/
dist_example_DATA = \
	examples/lbash.lua \
	examples/lecho.lua \
	examples/lgforce.lua \
	examples/limagemagic.lua \
	examples/lpostcmd.lua \
	examples/lrsync.lua \
	examples/lrsyncssh.lua
TESTS = \
	tests/churn-rsync.lua \
	tests/churn-rsyncssh.lua \
	tests/churn-direct.lua \
	tests/exclude-rsync.lua \
	tests/exclude-rsyncssh.lua \
	tests/l4rsyncdata.lua

dist_man1_MANS = doc/lsyncd.1
EXTRA_DIST = doc/lsyncd.1.txt doc/lsyncd.1.xml

doc/lsyncd.1: doc/lsyncd.1.xml
	xsltproc -o $@ -nonet /etc/asciidoc/docbook-xsl/manpage.xsl $<

doc/lsyncd.1.xml: doc/lsyncd.1.txt
	asciidoc -o $@ -b docbook -d manpage $<

if RUNNER
# installs the runner script
runnerdir = $(RUNNER_DIR)
runner_DATA = lsyncd.lua
else
# or compiles it into the binary
lsyncd_LDADD += luac.o

luac.o: luac.c

luac.c: luac.out bin2carray.lua
	lua ./bin2carray.lua luac.out luac luac.c

luac.out: lsyncd.lua
	luac $<

endif

