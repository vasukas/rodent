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

CMake, qmake (both Linux-only) and Visual Studio 2017 project files are provided.

Requires C++17 compiler with support for zero-argument variadic macros. Tested with GCC 9.1.0 and MSVC 2017.

