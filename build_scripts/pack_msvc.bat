if not exist ..\main.cpp exit

set platf=32
set vers=Release

set OutFile=rodent-msvc
set DirTop=tmp_pack_msvc

set Dir=%DirTop%\rodent

if %platf%==32 (
  set Exe=..\%vers%
)
if %platf%==64 (
  set Exe=..\x64\%vers%
)

set OutFile=%OutFile%%vers%
if %vers%==Debug (
  set OutFile=%OutFile%-debug
)

mkdir %Dir%

xcopy ..\res %Dir%\res
xcopy %Exe%\rodent.exe %Dir%

del /q %OutFile%
..\build_libs\7za.exe a %OutFile%.zip %Dir%

rmdir /s /q %DirTop%
