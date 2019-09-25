#!/bin/sh

# note: it's deleted using rm -rf!
TmpDir="${TMPDIR:-/tmp}"/rodent_load_deps

if [ ! -f "core/main.cpp" ]; then
	echo "Must be launched from project directory"
	exit 1
fi

OutDir="$PWD/external"
mkdir -p "$OutDir"



dlsrc() {
	mkdir "$TmpDir"
	cd "$TmpDir"

	git init
	git config core.sparsecheckout true
	git remote add -f origin "$1"

	SrcEnd=$2
	shift 2
	List=""

	for Var in "$@"
	do
		List="${List}${Var}
"
	done

	echo "$List" > .git/info/sparse-checkout
	git pull origin master
	
	if [ $SrcEnd -eq 1 ]; then
		for Var in "$@"
		do
			mv "$Var" "$OutDir"
		done
		dlsrc_end
	fi
}
dlsrc_end() {
	cd ..
	rm -rf "$TmpDir"
}

dlsrc "https://github.com/erincatto/Box2D.git" 1 "Box2D/"
dlsrc "https://github.com/nothings/stb.git" 1 "stb_image.h" "stb_image_write.h"

dlsrc "https://github.com/fmtlib/fmt.git" 0 "include/fmt/" "src/*.cc"
mv include/fmt "$OutDir"
mv src/*.cc "$OutDir/fmt"
dlsrc_end



echo "========================="

if [ ! -f /etc/os-release ]; then
	echo "Unknown OS, install FreeType2, GLEW and SDL2 manually"
else
	source /etc/os-release
	if [ "$ID"=="arch" ]; then
		echo "Install packages with:"
		echo "	pacman -S --needed freetype2 glew sdl2"
	elif [ "$ID"=="debian" ] || [ "$ID"=="ubuntu" ]; then
		echo "Install dev packages with:"
		echo "	apt-get install libfreetype6-dev libglew-dev libsdl2-dev"
		echo "Install only runtime packages with:"
		echo "	apt-get install libfreetype6 libglew libsdl2"
	else
		echo "Unknown distro, install FreeType2, GLEW and SDL2 manually"
	fi
fi

