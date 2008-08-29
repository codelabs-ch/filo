#!/bin/sh

#ALLCLEAN=1

OS=`uname -s`
if [ "$OS" == "Darwin" -o "${OS:0:6}" == "CYGWIN" ]; then
    MAKEFLAGS="			\
	AS=i386-elf-as		\
	CC=i386-elf-gcc		\
	AR=i386-elf-ar		\
	LD=i386-elf-ld		\
	STRIP=i386-elf-strip	\
	NM=i386-elf-nm		\
	HOSTCC=gcc		\
	-j
	"
fi
if [ "$OS" == "Linux" ]; then
MAKEFLAGS='CC="gcc -m32" LD="ld -b elf32-i386" HOSTCC="gcc"'
fi

if [ "$ALLCLEAN" != "" -o ! -r libpayload/build/lib/libpayload.a ]; then
  cd libpayload
  make clean
  make defconfig
  make $MAKEFLAGS
  cd ..
fi

make distclean
make defconfig
make $MAKEFLAGS

