#!/bin/bash

CONFIG=defconfig

for make in make gmake gnumake; do
	if [ "`$make --version 2>/dev/null | grep -c GNU`" -gt 0 ]; then
		MAKE=$make
		break
	fi
done

GCCPREFIX=invalid
for gccprefixes in `pwd`/../crossgcc/xgcc/bin/i386-elf- i386-elf- ""; do
	TMP=`mktemp /tmp/temp.XXXX`
	echo "mov %eax, %eax" > ${TMP}.s
	printf "\x7fELF" > ${TMP}.compare
	if which ${gccprefixes}as 2>/dev/null >/dev/null; then
		printf ""
	else
		continue
	fi
	if ${gccprefixes}as --32 -o ${TMP}.o ${TMP}.s; then
		dd bs=4 count=1 if=${TMP}.o > ${TMP}.test 2>/dev/null
		if cmp ${TMP}.test ${TMP}.compare; then
			GCCPREFIX=$gccprefixes
			rm -f $TMP ${TMP}.s ${TMP}.o ${TMP}.compare ${TMP}.test
			break
		fi
	fi
	rm -f $TMP ${TMP}.s ${TMP}.o ${TMP}.compare ${TMP}.test
done

if [ "$GCCPREFIX" = "invalid" ]; then
	echo no suitable gcc found
	exit 1
fi

MAKEFLAGS=" \
	AS=\"${GCCPREFIX}as --32\"		\
	CC=\"${GCCPREFIX}gcc -m32\"		\
	AR=\"${GCCPREFIX}ar\"			\
	LD=\"${GCCPREFIX}ld -b elf32-i386\"	\
	STRIP=\"${GCCPREFIX}strip\"		\
	NM=\"${GCCPREFIX}nm\"			\
	HOSTCC=gcc				\
	-j					\
"

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

