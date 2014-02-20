#!/bin/sh

echo "MacPort needed packages: autogen lua asciidoc"

export PATH=/opt/local/bin:$PATH

echo "Generating configure files... may take a while."

rm -rf OSX
mkdir OSX
for f in *; do
	ln -s ../$f OSX
done
rm -f OSX/OSX OSX/configure.ac
m4 -DPKG_CHECK_MODULES < configure.ac > OSX/configure.ac
cd OSX

xnu=`uname -a | sed 's,.*\(xnu[^\~]*\).*,\1,'`
xnurl="http://www.opensource.apple.com/source/xnu/$xnu/bsd/sys/fsevents.h?txt"
mkdir -p bsd/sys
echo "downloading '$xnurl' to bsd/sys/fsevents.h"
curl "$xnurl" > bsd/sys/fsevents.h

if autoreconf --install --force; then
  echo "Preparing was successful if there was no error messages above."
  echo "Running configure..."
  
  # does not work.. ?
  export PKG_CONFIG_PATH=/opt/local/lib/pkgconfig:$PKG_CONFIG_PATH
  # this works
  export CFLAGS="-I/opt/local/include"
  export LDFLAGS="-L/opt/local/lib -llua"
  
  # problem with O_CLOEXEC /usr/include/sys/fcntl.h on 10.6
  grep O_CLOEXEC /usr/include/sys/fcntl.h 2>&1 >/dev/null || CFLAGS="$CFLAGS -DO_CLOEXEC=0"
  
  echo $CFLAGS
  
  ./configure --without-inotify --with-fsevents --prefix=/usr/local
  
  echo "You can run (cd OSX; make)"
fi
