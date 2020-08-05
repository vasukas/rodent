## Overview

Simple 2D top-down shooter with "neon wireframe"-style graphics using SDL2, OpenGL and Box2D.

### Screenshots

![](../screenshots/1.gif?raw=true)

<table frame="void">
  <tr><td><img src="../screenshots/1.png?raw=true" /></td>
  <td><img src="../screenshots/2.png?raw=true" /></td>
  <td><img src="../screenshots/3.png?raw=true" /></td>
  <td><img src="../screenshots/4.png?raw=true" /></td>
  <td><img src="../screenshots/5.png?raw=true" /></td>
  </tr>
</table>

## Build

Requires git, CMake and compiler with full C++17 support.

### Linux

Install binary dependecies:

	$ sudo apt-get install libfreetype6-dev libglew-dev libopusfile-dev libsdl2-dev  # Ubuntu/Debian
	$ sudo apt-get install libfreetype6 libglew libsdl2     # Ubuntu/Debian (only runtime)
	$ sudo pacman -S --needed freetype2 glew opusfile sdl2  # Arch Linux

Source dependecies (Box2D, fmt, stb) are downloaded automatically by CMake.  
If enabled but not installed, opusfile and PortAudio also will be downloaded and built by CMake.

Run `scripts/build.sh` from project root to build with CMake.
Resulting file will be saved as `build/rodent`.
You may copy it to another directory along with data by specifying `-DPKG_OUTPUT_DIR=` option
with destination path.

### Windows

Requires Visual C++ 2015/2017/2019 redistributable at runtime:
	https://aka.ms/vs/16/release/vc_redist.x64.exe, 
	https://aka.ms/vs/16/release/vc_redist.x86.exe

Run in PowerShell from project root:
* if script execution is disabled, run `Set-ExecutionPolicy -ExecutionPolicy Unrestricted -Scope Process`;
* run `scripts/load_deps.ps1` to download binary dependecies;
* run `scripts/build.ps1`:
  * specify `-bits 32` option to build as 32-bit instead of default 64-bit;
  * specify `-pack` option to write runtime files to `rodent.zip` in project root.

