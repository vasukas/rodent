# PowerShell downloader for MSVC

$Git="git.exe"
$TmpDir="load_deps_tmp"
$TmpZip="load_deps_tmp.zip"



if (!(Test-Path "core\main.cpp"))
{
	if (!(Test-Path "..\core\main.cpp"))
	{
		Write-Host "Must be launched from project root"
		exit 1
	}
	Set-Location ".."
}

$GitVars="$Git","git.exe","C:\Program Files\Git\cmd\git.exe","C:\Program Files\Git\bin\git.exe"
$Git=""

ForEach ($Var in $GitVars) {
	if ((Get-Command "$Var" -ErrorAction SilentlyContinue) -ne $null)
	{
		$Git="$Var"
		break
	}
}

if ("$Git") {
	Write-Host "Found git at $Git"
}
else {
	Write-Host "Unable to find git"
	exit 1
}

$OutDir=Get-Location
New-Item -ItemType Directory -Force -Path "$OutDir" -Name "external"

$OutDir="$OutDir/external"
New-Item -ItemType Directory -Force -Path "$OutDir" -Name "include"
New-Item -ItemType Directory -Force -Path "$OutDir" -Name "msvc32"
New-Item -ItemType Directory -Force -Path "$OutDir" -Name "msvc64"



function dlsrc($name, $SrcEnd, $files)
{
	New-Item -ItemType Directory -Name "$TmpDir"
	cd "$TmpDir"

	&"$Git" init
	&"$Git" config core.sparsecheckout true
	&"$Git" remote add -f origin "$name"

	$List=$files -join "`n" | Out-String
	
	"$List" | Out-File -Encoding ASCII -FilePath ".git/info/sparse-checkout"
	&"$Git" pull origin master

	if ($SrcEnd -eq 1) {
		ForEach ($Var in $files) {
			Move-Item -Path "$Var" -Destination "$OutDir"
		}
		dlsrc_end
	}
}
function dlsrc_end()
{
	cd ..
	Remove-Item -Recurse -Force "$TmpDir"
}



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
	Invoke-WebRequest -Uri "$url" -OutFile "$TmpZip" -UserAgent [Microsoft.PowerShell.Commands.PSUserAgent]::FireFox
	
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



dlsrc "https://github.com/nothings/stb.git" 1 ("stb_image.h", "stb_image_write.h")

dlsrc "https://github.com/erincatto/Box2D.git" 0 ("include", "src")
Move-Item -Path "include/box2d" -Destination "$OutDir"
Move-Item -Path "src/*" -Destination "$OutDir/box2d"
dlsrc_end

dlsrc "https://github.com/fmtlib/fmt.git" 0 ("include/fmt", "src/*.cc")
Move-Item -Path "include/fmt" -Destination "$OutDir"
Move-Item -Path "src/*.cc" -Destination "$OutDir/fmt"
dlsrc_end

dlzip "https://sourceforge.net/projects/glew/files/glew/2.1.0/glew-2.1.0-win32.zip/download" `
	"glew*/include/GL" `
	("glew*/lib/Release/Win32/glew32.lib", "glew*/bin/Release/Win32/glew32.dll") `
	("glew*/lib/Release/x64/glew32.lib", "glew*/bin/Release/x64/glew32.dll")

dlzip "https://github.com/ubawurinna/freetype-windows-binaries/archive/master.zip" `
	("freetype*/include/freetype", "freetype*/include/*.h") `
	"freetype*/win32/*.*" `
	"freetype*/win64/*.*"

dlzip "https://www.libsdl.org/release/SDL2-devel-2.0.10-VC.zip" `
	"SDL2*/include" `
	"SDL2*/lib/x86/SDL2.*" `
	"SDL2*/lib/x64/SDL2.*" `

Move-Item -Path "$OutDir/include/include" -Destination "$OutDir/include/SDL2"

