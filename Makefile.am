AUTOMAKE_OPTIONS = foreign
CFLAGS = -Wall $(LIBLUA_CFLAGS) 
bin_PROGRAMS = lsyncd
lsyncd_SOURCES = lsyncd.c 
lsyncd_LDADD = $(LIBLUA_LIBS)

if RUNNER
else
lsyncd: luac.o $(lsyncd_LDADD) 

architecture: | lsyncd.o
	objdump -f $< | grep architecture | sed -e "s/,.*$$//" -e "s/[^ ]* \(.*\)/\1/" > $@ || rm architecture

luac.o: luac.out architecture
	objcopy --input-target=binary --output-target=`gcc -dumpmachine` --binary-architecture=`cat architecture` $< $@

luac.out: lsyncd.lua
	luac $<
endif

