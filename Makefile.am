AUTOMAKE_OPTIONS = foreign
CFLAGS = -Wall $(LIBLUA_CFLAGS) 
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.h lsyncd.c lsyncd.lua
if INOTIFY
lsyncd_SOURCES += inotify.c
endif
if FSEVENTS
lsyncd_SOURCES += fsevents.c
endif

lsyncd_LDADD = $(LIBLUA_LIBS)
exampledir = $(datarootdir)/doc/@PACKAGE@
dist_example_DATA = \
	examples/lbash.lua \
	examples/lecho.lua \
	examples/lgforce.lua \
	examples/limagemagic.lua \
	examples/lrsync.lua \
	examples/lrsyncssh.lua
TESTS = tests/l4-rsync-data.sh


if RUNNER

# installs the runner
runnerdir = $(RUNNER_DIR)
runner_DATA = lsyncd.lua

else

# or compiles it into the binary

lsyncd_LDADD += luac.o

objarch: | lsyncd.o
	objdump -f lsyncd.o | grep architecture | \
		sed -e "s/,.*$$//" -e "s/[^ ]* \(.*\)/\1/" > $@ || rm $@

objtarget: | lsyncd.o
	objdump -f lsyncd.o | grep "file format" | \
		sed -e "s/.* \(.*\)/\1/" > $@ || rm $@

luac.o: luac.out objarch objtarget
	objcopy --input-target=binary \
		--output-target=`cat objtarget` \
		--binary-architecture=`cat objarch` $< $@

luac.out: lsyncd.lua
	luac $<

endif

