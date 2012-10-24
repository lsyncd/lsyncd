ACLOCAL_AMFLAGS = -I m4

CFLAGS += -Wall $(LUA_CFLAGS)
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.h lsyncd.c lsyncd.lua default-rsync.lua

if INOTIFY
lsyncd_SOURCES += inotify.c
endif

if FSEVENTS
lsyncd_SOURCES += fsevents.c
endif

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
	tests/schedule.lua \
	tests/l4rsyncdata.lua

dist_man1_MANS = doc/lsyncd.1
EXTRA_DIST = doc/lsyncd.1.txt inotify.c fsevents.c bin2carray.lua \
	default.lua default-rsync.lua default-rsyncssh.lua default-direct.lua

doc/lsyncd.1: doc/lsyncd.1.txt
	$(A2X) --format=manpage $<

CLEANFILES = runner.out defaults.out runner.c defaults.c

# compiles the runner and the defaults into the binary
lsyncd_LDADD += runner.o defaults.o

runner.o: runner.c
defaults.o: defaults.c

runner.c: runner.out bin2carray.lua
	$(LUA) ./bin2carray.lua $< runner $@

defaults.c: defaults.out bin2carray.lua
	$(LUA) ./bin2carray.lua $< defaults $@

runner.out: lsyncd.lua
	$(LUAC) -o $@ $<

defaults.out: default.lua default-rsync.lua default-rsyncssh.lua default-direct.lua
	$(LUAC) -o $@ $^

