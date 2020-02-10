#!/bin/sh
# All command line arguments are passed to CMake

if [ ! -f core/main.cpp ]; then
	[ -f ../core/main.cpp ] && cd ..
	if [ $? -ne 0 ]; then
		echo "Must be launched from project root"
		exit 1
	fi
fi

mkdir -p build && cd build && cmake "$@" .. && cmake --build .
if [ $? -ne 0 ]; then
	exit 1
fi
cd ..

OutDir=rodent

mkdir -p "$OutDir"
cp build/rodent "$OutDir"
cp -r data "$OutDir"

