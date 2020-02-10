## Overview

Simple 2D top-down shooter with "neon wireframe"-style graphics using SDL2, OpenGL and Box2D.

## Usage

Press F1 to see a control summary, ESC to list all keybindings.

Run `rodent --help` to list command-line options.

Default settings are overriden by `user/settings.cfg`, if present. Use `data/settings.cfg.default` as a template.

## Build

Requires C++17 compiler with support for zero-argument variadic macros. Tested with GCC 9.1.0 and MSVC 2017.

### Linux

Download binary dependecies:

	$ sudo apt-get install libfreetype6-dev libglew-dev libsdl2-dev  # Ubuntu/Debian (build)
	$ sudo apt-get install libfreetype6 libglew libsdl2              # Ubuntu/Debian (only runtime)
	$ sudo pacman -S --needed freetype2 glew sdl2                    # Arch Linux

Source dependecies (Box2D, fmt, stb) are downloaded by CMake.

Run `scripts/build.sh` to build with CMake and copy all game files to `rodent` directory.

### Windows

* Run `scripts/load_deps.ps1` to download all dependecies.
* Build using Visual Studio 2017 solution.
* Run `scripts/pack.bat` to copy all game files (including DLLs) to `rodent` directory.

Not tested, but should also build with CMake using `scripts/build.bat`.

