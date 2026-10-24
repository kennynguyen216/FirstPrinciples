# Downloads premake and the SDL3 dev package into ThirdParty/.
# Qt is installed separately and found through QT_DIR (see README.md).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ThirdParty = Join-Path $Root "ThirdParty"
New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null

$PremakeVersion = "5.0.0"
$SdlVersion = "3.4.18"

function Get-Zip($Url, $Dest) {
    $zip = Join-Path $env:TEMP ([IO.Path]::GetFileName($Url))
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $Dest -Force
    Remove-Item $zip
}

$PremakeDir = Join-Path $ThirdParty "premake"
if (-not (Test-Path (Join-Path $PremakeDir "premake5.exe"))) {
    Get-Zip "https://github.com/premake/premake-core/releases/download/v$PremakeVersion/premake-$PremakeVersion-windows.zip" $PremakeDir
}

$SdlDir = Join-Path $ThirdParty "SDL3"
if (-not (Test-Path (Join-Path $SdlDir "include"))) {
    $tmp = Join-Path $ThirdParty "sdl_tmp"
    Get-Zip "https://github.com/libsdl-org/SDL/releases/download/release-$SdlVersion/SDL3-devel-$SdlVersion-VC.zip" $tmp
    Move-Item (Join-Path $tmp "SDL3-$SdlVersion") $SdlDir
    Remove-Item $tmp -Recurse
}

if (-not $env:QT_DIR) {
    Write-Warning "QT_DIR is not set. Set it to your Qt kit, for example C:\Qt\6.8.3\msvc2022_64"
}

Write-Host "Done. Run Scripts\GenerateProjects.bat"
