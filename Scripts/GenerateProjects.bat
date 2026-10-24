@echo off
rem Generates the Visual Studio 2022 solution in Build\.
pushd "%~dp0.."
if not exist ThirdParty\premake\premake5.exe (
    echo Run Scripts\Setup.ps1 first.
    popd
    exit /b 1
)
ThirdParty\premake\premake5.exe vs2022
popd
echo Open Build\FirstPrinciples.sln
