## Overview

Simple action game with "neon vector"-style graphics.

All controls are listed in 'res/help.txt'.

Should work on any platform with keyboard where SDL2 and OpenGL 3.3 are available.

Source code is licensed under MIT license.

## Build

Overly complex, later will be replaced with CMake.

Requires C++17 compiler with support for zero-argument variadic macros, tested only with GCC 9.1.0 and MSVC 2017.

For vb.sh builds (Linux, MinGW) - call it without arguments to see usage summary.

All builds require Box2D source in root directory.

### Linux

Install dependecies:

	$ pacman -S glew fmt freetype2 sdl2

#### qmake

File: rodent.pro. By default set to compile with GCC/clang sanitizer. Doesn't depend on Qt library.

Build project (from root directory):

	$ mkdir build
	$ (cd build && qmake ../rodent.pro -spec linux-g++ CONFIG+=release && make)
	$ make --directory=build/qt/

Compiled executable will be placed in 'build/qt'

#### bash script

Build project (from root directory):

	$ (cd build_scripts && ./vb.sh linux 2)

Compiled executable will be placed in 'build/vb_bin' (no extension).

### Windows

Dependecies can be either linked or built-in. Latter placed in build_libs/include for headers and build_libs/src for sources.

Libraries placed in 'build_libs/msvc' or 'build_libs/mingw' depending or build system used, and placed in separate directories there - 'include' for headers, 'lib32' and 'lib64' for static and dynamic libraries.

Required libraries can be downloaded at:

* GLEW - <http://glew.sourceforge.net/> (binaries & source)
* fmt - <https://fmt.dev/latest/index.html> (source)
* FreeType2
    * VS binaries - <https://github.com/ubawurinna/freetype-windows-binaries>
    * MinGW binaries - <http://gnuwin32.sourceforge.net/packages/freetype.htm>
    * source - <https://sourceforge.net/projects/freetype/files/freetype2>
* SDL2 - <https://www.libsdl.org/download-2.0.php> (binaries & source)

SDL2 2.0.9 can cause lag spikes on Windows. Minimal required version - 2.0.4.

#### Visual Studio

Requires dependecies to be placed in 'build_libs/msvc'.

Microsoft Visual Studio 2017 solution and project files: rodent.sln and rodent.vcxproj. All paths are set as relative.

Compiled executable will be placed in 'build/Release' or 'build/Debug'.

Before debugging run 'build_scripts/msvc_run_prepare.bat', which would setup special working directories with DLLs and softlink to folder with game resources.

#### Cross-compile from Linux (MinGW, bash script)

_Seems to be broken_

Requires dependecies to be placed in 'build_libs/mingw'. Additionaly needs 'libwinpthread-1.dll' at runtime (/usr/i686-w64-mingw32/bin/libwinpthread-1.dll on Arch Linux).

Before build check compiler and linker executable names - set in 'build_scripts/vb_tar_win' as 'Compiler' and 'CompilerLnk'.

Only 32-bit build with Mingw-w64 was tested.

Build project (from root directory):

	$ (cd build_scripts && ./vb.sh win 2)

Compiled executable will be placed in 'build/vb_bin' (as '.exe').
