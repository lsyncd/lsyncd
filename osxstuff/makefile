
MAIN = lsyncd

CFLAGS		+= -Wall -Wextra
CFLAGS		+= -g
LDFLAGS		+=

OSXMAIN = lsyncd
OSXSRC	= fsevents-api fsevents-bindapi runner defaults

ARCH	= undefined

ifeq ($(shell uname),Darwin)
ARCH	= darwin
CFLAGS	+= -I/opt/local/include/
LDFLAGS += -framework CoreServices -framework CoreFoundation
LDFLAGS	+= -L/opt/local/lib -llua
MAIN	+= $(OSXMAIN)
SRC	+= $(OSXSRC)
$(shell cp osxstuff/* .)
endif

#ifeq ($(shell uname),Linux)
#CFLAGS	+= -DINOTIFY=1
#ARCH	= linux
#endif

ifeq ($(ARCH),undefined)
$(error wip makefile, ARCH still undefined)
endif

all: $(MAIN)

help:
	@echo "help"
	
%: %.o $(SRC:%=%.o)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MD -MF $(@:%.o=.$(ARCH).%.d) -c $<

clean:
	rm -f *.o .$(ARCH).*.d $(MAIN) $(SRC:%=%.o)

distclean: clean
	rm -f *~ *.o .*.*.d $(OSXMAIN:%=%.o) $(OSXSRC:%=%.o) $(LINUXMAIN:%=%.o) $(LINUXSRC:%=%.o)

.SUFFIXES: # no implicit rules
.PRECIOUS: $(MAIN:%=%.o) $(SRC:%=%.o)
-include .$(ARCH).*.d
