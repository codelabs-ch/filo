#!/bin/bash

CONFIG=defconfig

for make in make gmake gnumake; do
	if [ "`$make --version 2>/dev/null | grep -c GNU`" -gt 0 ]; then
		MAKE=$make
		break
	fi
done

OS=`uname -s`
if [ "$OS" = "Darwin" -o "$OS" = "SunOS" -o "${OS:0:6}" = "CYGWIN" ]; then
    MAKEFLAGS="			\
	AS=i386-elf-as		\
	CC=i386-elf-gcc		\
	AR=i386-elf-ar		\
	LD=i386-elf-ld		\
	STRIP=i386-elf-strip	\
	NM=i386-elf-nm		\
	HOSTCC=gcc		\
	-j			\
	"
fi
if [ "$OS" = "Linux" ]; then
    MAKEFLAGS='CC="gcc -m32" LD="ld -b elf32-i386" HOSTCC="gcc" AS="as --32"'
fi

$MAKE distclean
cp configs/$CONFIG ./.config
$MAKE oldconfig

cd libpayload
$MAKE distclean
cp configs/$CONFIG .config
$MAKE oldconfig
eval $MAKE $MAKEFLAGS
eval $MAKE $MAKEFLAGS DESTDIR=../build install 
cd ..

eval $MAKE $MAKEFLAGS

