## Overview

Simple top-down shooter with "neon wireframe" graphics using SDL2, OpenGL and Box2D.

## Controls

Game (default)

	Press ESCAPE to see all key bindings

Special

| Key | Command |
| --- | --- |
| ~ + Q   | exit immediatly
| ~ + R   | reload shaders
| ~ + F   | toggle fullscreen
| ~ + S   | reset window size
| ~ + 1-3 | debug menues
| F2      | toggle log display

## Build

Requires C++17 compiler with support for zero-argument variadic macros. Tested with GCC 9.1.0 and MSVC 2017.

### Linux

To download needed sources (from project root):

	$ build_scripts/load_deps.sh

Needed packages can be downloaded with:

	$ sudo apt-get install libfreetype6-dev libglew-dev libsdl2-dev  # Ubuntu/Debian (build)
	$ sudo apt-get install libfreetype6 libglew libsdl2              # Ubuntu/Debian (runtime)
	$ sudo pacman -S --needed freetype2 glew sdl2                    # Arch

After that project can be built using CMake or qmake.

	$ (mkdir -p build && cd build && cmake .. && make)

Executable will be placed in "build/" directory in project root, so game can be run from there as:

	$ build/rodent

### Windows

To download dependecies execute in PowerShell (from project root):

	Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
	build_scripts/load_deps.ps1

After that project can be built using Visual Studio 2017 or newer.
Executable will be placed in "build\" directory in project root.
