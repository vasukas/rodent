if not exist ..\main.cpp exit

setlocal enableextensions enabledelayedexpansion

set Dir=..\build\run_msvc
set LibDir=..\build_libs\msvc\lib
mkdir %Dir%

mkdir %Dir%\x32
mklink /d %Dir%\x32\res ..\..\..\res
for %%x in (%LibDir%32\*.dll) do xcopy /y %%x %Dir%\x32

mkdir %Dir%\x64
mklink /d %Dir%\x64\res ..\..\..\res
for %%x in (%LibDir%64\*.dll) do xcopy /y %%x %Dir%\x64
