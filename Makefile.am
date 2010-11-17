AUTOMAKE_OPTIONS = foreign
CFLAGS = -Wall $(LIBLUA_CFLAGS) 
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.c lsyncd.lua 
lsyncd_LDADD = $(LIBLUA_LIBS)
exampledir = $(datarootdir)/doc/@PACKAGE@
dist_example_DATA = \
	examples/lbash.lua \
	examples/limagemagic.lua \
	examples/lrsync.lua \
	examples/lrsyncssh.lua

if RUNNER

# installs the runner
runnerdir = $(RUNNER_DIR)
runner_DATA = lsyncd.lua

else

# or compiles it into the binary
lsyncd: luac.o $(lsyncd_LDADD) 

architecture: | lsyncd.o
	objdump -f lsyncd.o | grep architecture | sed -e "s/,.*$$//" -e "s/[^ ]* \(.*\)/\1/" > $@ || rm architecture

luac.o: luac.out architecture
	objcopy --input-target=binary --output-target=`gcc -dumpmachine` --binary-architecture=`cat architecture` $< $@

luac.out: lsyncd.lua
	luac $<

endif

