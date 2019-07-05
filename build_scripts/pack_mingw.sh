#!/bin/bash

if [ "$1" != 32 ] && [ "$1" != 64 ]; then
	echo "Specify bits (32 or 64)"
	exit 1
fi

Bits=$1
OutFile=build/rodent-mingw$Bits.zip

if [ ! -f ../main.cpp ]; then
	echo "Not launched from 'build' directory"
	exit 1
fi
cd ..

Input=rodent-mingw$Bits
InputPath=/tmp
Wp="$InputPath"/"$Input"

if [ -d "$Wp" ]; then
	echo "Temporary path already exists"
	exit 1
fi
mkdir -p "$Wp"

ln -s "$(pwd)"/res "$Wp"/res
cp "$(pwd)"/build/vb_bin/rodent_x$Bits.exe "$Wp"

for F in build_libs/mingw/lib$Bits/*.dll; do
	ln -s "$(pwd)"/"$F" "$Wp"/$(basename "$F")
done

Output="$(pwd)"/"$OutFile"
cd "$InputPath"
zip -r -0 "$Output" "$Input"

rm -r "$Wp"
