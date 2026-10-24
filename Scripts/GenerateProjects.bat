@echo off
rem Generates a solution for the installed Visual Studio C++ toolset in Build\.
setlocal
pushd "%~dp0.."
if not exist ThirdParty\premake\premake5.exe (
    echo Run Scripts\Setup.ps1 first.
    popd
    exit /b 1
)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Install Visual Studio 2022 or 2026 with Desktop development with C++.
    popd
    exit /b 1
)
set "VS_VERSION="
for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VS_VERSION=%%V"
set "ACTION="
if "%VS_VERSION:~0,3%"=="17." set "ACTION=vs2022"
if "%VS_VERSION:~0,3%"=="18." set "ACTION=vs2026"
if not defined ACTION (
    echo No supported Visual Studio C++ toolset found. Install Visual Studio 2022 or 2026 C++ build tools.
    popd
    exit /b 1
)
ThirdParty\premake\premake5.exe %ACTION%
set "RESULT=%ERRORLEVEL%"
set "SOLUTION=FirstPrinciples.sln"
if "%ACTION%"=="vs2026" set "SOLUTION=FirstPrinciples.slnx"
if "%RESULT%"=="0" echo Open Build\%SOLUTION%
popd
exit /b %RESULT%
