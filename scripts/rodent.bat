if "%PROCESSOR_ARCHITECTURE%"=="x86" (
	if exist rodent_x32d.exe (
		rodent_x32d %*
	) else (
		rodent_x32 %*
	)
) else (
	if exist rodent_x64d.exe (
		rodent_x64d %*
	) else (
		rodent_x64 %*
	)
)
