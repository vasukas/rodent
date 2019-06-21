#!/bin/bash

OutFile=build/rodent-mingw32.zip

if [ ! -f ../main.cpp ]; then
	echo "Not launched from 'build' directory"
	exit 1
fi
cd ..

Input=rodent-mingw32
InputPath=/tmp
Wp="$InputPath"/"$Input"

if [ -d "$Wp" ]; then
	echo "Temporary path already exists"
	exit 1
fi
mkdir -p "$Wp"

ln -s "$(pwd)"/res "$Wp"/res
cp "$(pwd)"/build/vb_bin/rodent.exe "$Wp"

for F in build_libs/mingw/lib32/*.dll; do
	ln -s "$(pwd)"/"$F" "$Wp"/$(basename "$F")
done

Output="$(pwd)"/"$OutFile"
cd "$InputPath"
zip -r -0 "$Output" "$Input"

rm -r "$Wp"
