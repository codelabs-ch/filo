#!/bin/bash

if [ "$1" == "" ]; then 
	CONFIG=defconfig
fi

build_with_config()
{
	cp configs/$1 ./.config
	$MAKE oldconfig
	$MAKE
}

for make in make gmake gnumake; do
	if [ "`$make --version 2>/dev/null | grep -c GNU`" -gt 0 ]; then
		MAKE=$make
		break
	fi
done

FILO=$PWD
$MAKE distclean
cd ../coreboot/payloads/libpayload
$MAKE distclean
build_with_config $CONFIG
$MAKE DESTDIR=$FILO/build install 
cd $FILO
build_with_config $CONFIG

