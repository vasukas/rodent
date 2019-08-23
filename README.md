## Overview

Simple top-down shooter with "neon wireframe" graphics.

System requirements: SDL2 and OpenGL 3.3 support, keyboard & mice / gamepad.

## Controls

Game (default)

| Key | Command |
| --- | --- |
| W,A,S,D | movement
| Space   | accelerate
| LMB     | fire
| RMB     | alt. fire / aim
| 1-7     | switch weapon
| Escape  | menu

Special

| Key | Command |
| --- | --- |
| Ctrl + Q | exit immediatly
| Ctrl + R | reload shaders
| Ctrl + F | toggle fullscreen
| ~ + 1-3  | debug menues

## Build

Requires C++17 compiler with support for zero-argument variadic macros. Tested with GCC 9.1.0 and MSVC 2017.

### Linux

To download needed sources (from project root):

	$ build_scripts/load_deps.sh

Needed packages can be downloaded with:

* Arch Linux

		$ pacman -S --needed freetype2 glew sdl2

* Ubuntu/Debian (build & runtime)

		$ apt-get install libfreetype6-dev libglew-dev libsdl2-dev

* Ubuntu/Debian (runtime only)

		$ apt-get install libfreetype6 libglew libsdl2

After that project can be built using CMake or qmake.

	$ (mkdir -p build && cd build && cmake .. && make)

Executable will be placed in "build\" directory in project root.

### Windows

To download dependecies execute in PowerShell (from project root):

	Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
	build_scripts/load_deps.ps1

After that project can be built using Visual Studio 2017 or newer.
Executable will be placed in "build\" directory in project root.

