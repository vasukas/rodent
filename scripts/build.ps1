# other command line arguments are passed to CMake
param (
	[int]$bits = 64,
	[switch]$pack = $false
)
$ErrorActionPreference = 'Stop'

$PackName="rodent$bits.zip"
$PackDir="rodent$bits"

if ($bits -eq 64) {
	$Platform='-G', 'Visual Studio 15 2017 Win64'
}
elseif ($bits -ne 32) {
	Write-Host "Invalid 'bits' value"
	exit 1
}

if (!(Test-Path "core\main.cpp"))
{
	if (!(Test-Path "..\core\main.cpp"))
	{
		Write-Host "Must be launched from project root"
		exit 1
	}
	Set-Location ".."
}

$External="$(Get-Location)\external"
if (Test-Path "$External\lib") {
	[io.directory]::Delete("$External\lib")
}
New-Item -Path "$External\lib" -ItemType Junction -Value "$External\msvc$bits"

if ($pack) {
	$PackDir="$(Get-Location)\$PackDir"
	$PackName="$(Get-Location)\$PackName"
	$args += "-DPKG_OUTPUT_DIR=$PackDir"
}



$CmakeVars="cmake.exe","C:\Program Files\CMake\bin\cmake"
ForEach ($Var in $CmakeVars) {
	if ((Get-Command "$Var" -ErrorAction SilentlyContinue) -ne $null)
	{
		$Cmake="$Var"
		break
	}
}

if ("$Cmake") {
	Write-Host "Found CMake at $Cmake"
}
else {
	Write-Host "Unable to find CMake"
	exit 1
}

New-Item -ItemType Directory -Force -Path "$(Get-Location)" -Name "build$bits"
cd "build$bits"

&"Cmake" $args $Platform "-DCMAKE_PREFIX_PATH=$External" ..
if (!($?)) {
	cd ..
	exit 1
}

&"Cmake" --build . --config MinSizeRel
if (!($?)) {
	cd ..
	exit 1
}
cd ..

if ($pack) {
	Add-Type -Assembly System.IO.Compression.FileSystem
	if (Test-Path "$PackName") {
		Remove-Item "$PackName"
	}
	[System.IO.Compression.ZipFile]::CreateFromDirectory(
		"$PackDir", "$PackName", [System.IO.Compression.CompressionLevel]::Optimal, $false)
}

