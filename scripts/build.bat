rem All command line arguments directly passed to CMake

where cmake
if %errorlevel% NEQ 0 (
	if not exist "C:\Program Files\CMake\bin\cmake" (
		echo "ERROR: CMake not found"
		pause
		exit /b 1
	) else (
		set cmake_exe="C:\Program Files\CMake\bin\cmake"
	)
) else (
	set cmake_exe=cmake
)

if not exist build\NUL (
	mkdir build
	if %errorlevel% NEQ 0 (
		echo "ERROR: can't create build directory"
		pause
		exit /b 1
	)
)

cd build && %cmake_exe% %* .. && %cmake_exe% --build .
if %errorlevel% NEQ 0 (
	echo "ERROR: build failed"
	pause
	exit /b 1
)

echo "FINISHED"
pause

