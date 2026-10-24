@echo off
rem Generates a solution for the installed Visual Studio C++ toolset in the project's Build folder.
rem Usage: Scripts\GenerateProjects.bat [Projects\Name]
setlocal
pushd "%~dp0.."
set "PROJECT=%~1"
if not defined PROJECT set "PROJECT=Projects\Sample"
for %%P in ("%PROJECT%") do set "PROJECT_NAME=%%~nxP"
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
ThirdParty\premake\premake5.exe --project="%PROJECT%" %ACTION%
set "RESULT=%ERRORLEVEL%"
set "SOLUTION=%PROJECT_NAME%.sln"
if "%ACTION%"=="vs2026" set "SOLUTION=%PROJECT_NAME%.slnx"
if "%RESULT%"=="0" echo Open %PROJECT%\Build\%SOLUTION%
popd
exit /b %RESULT%
