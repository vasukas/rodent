# PowerShell downloader for MSVC

param (
	[switch]$openmpt = $false
)
$ErrorActionPreference = 'Inquire'

$Git="git.exe"
$TmpDir="TMP_load_deps"
$TmpZip="TMP_load_deps.zip"



if (!(Test-Path "core\main.cpp"))
{
	if (!(Test-Path "..\core\main.cpp"))
	{
		Write-Host "Must be launched from project root"
		exit 1
	}
	Set-Location ".."
}

$OutDir="$(Get-Location)"
if (Test-Path "$OutDir/external") {
	Remove-Item -Recurse -Force "$OutDir/external"
}
New-Item -ItemType Directory -Path "$OutDir" -Name "external"

$OutDir="$OutDir/external"
New-Item -ItemType Directory -Force -Path "$OutDir" -Name "include"
New-Item -ItemType Directory -Force -Path "$OutDir" -Name "msvc32"
New-Item -ItemType Directory -Force -Path "$OutDir" -Name "msvc64"



function unzip($file, $dest)
{
	$file = (Resolve-Path "$file").Path
	$dest = (Resolve-Path "$dest").Path

	$shell = New-Object -ComObject shell.application
	$zip = $shell.NameSpace("$file")
	$dst = $shell.NameSpace("$dest")
	
	$dst.Copyhere($zip.items())
}

function dlzip($url, $include, $lib32, $lib64)
{
	Invoke-WebRequest -Uri "$url" -OutFile "$TmpZip"
	
	New-Item -ItemType Directory -Name "$TmpDir"
	unzip "$TmpZip" "$TmpDir/"
	
	ForEach ($Var in $include) {
		Move-Item -Path "$TmpDir/$Var" -Destination "$OutDir/include"
	}
	ForEach ($Var in $lib32) {
		Move-Item -Path "$TmpDir/$Var" -Destination "$OutDir/msvc32"
	}
	ForEach ($Var in $lib64) {
		Move-Item -Path "$TmpDir/$Var" -Destination "$OutDir/msvc64"
	}
	
	Remove-Item -Recurse -Force "$TmpDir"
	Remove-Item "$TmpZip"
}



dlzip "https://github.com/nigels-com/glew/releases/download/glew-2.1.0/glew-2.1.0-win32.zip" `
	"glew*/include/GL" `
	("glew*/lib/Release/Win32/glew32.lib", "glew*/bin/Release/Win32/glew32.dll") `
	("glew*/lib/Release/x64/glew32.lib", "glew*/bin/Release/x64/glew32.dll")

dlzip "https://github.com/ubawurinna/freetype-windows-binaries/archive/master.zip" `
	("freetype*/include/freetype", "freetype*/include/*.h") `
	"freetype*/win32/*.*" `
	"freetype*/win64/*.*"

dlzip "https://www.libsdl.org/release/SDL2-devel-2.0.12-VC.zip" `
	"SDL2*/include" `
	("SDL2*/lib/x86/SDL2.*", "SDL2*/lib/x86/SDL2main.*") `
	("SDL2*/lib/x64/SDL2.*", "SDL2*/lib/x64/SDL2main.*")

Rename-Item "$OutDir/include/include" "$OutDir/include/SDL2"

if ($openmpt) {
	dlzip "https://lib.openmpt.org/files/libopenmpt/dev/libopenmpt-0.4.12+release.dev.win.vs2017.zip" `
		"inc/libopenmpt" `
		("lib/x86/*.lib", "bin/x86/*.dll") `
		("lib/x86_64/*.lib", "bin/x86_64/*.dll")
}

