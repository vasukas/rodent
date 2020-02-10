set OUTDIR=rodent

if "%1"=="" (
	set BITS=64
) else (
	set BITS=%1
)

if not exist core\main.cpp (
	if not exist ..\core\main.cpp (
		echo "Must be launched from project root"
		exit /b 1
	)
	cd ..
)

mkdir %OUTDIR%
mkdir %OUTDIR%\data
xcopy /y /s data %OUTDIR%\data

copy /y scripts\rodent.bat %OUTDIR%
copy /y build\rodent_x%BITS%.exe %OUTDIR%
FOR %%f IN (external\msvc%BITS%\*.dll) DO copy /y %%f %OUTDIR%

