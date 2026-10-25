# Building FirstPrinciples

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The build scripts generate project-specific builds of the engine, editor, runtime and tools. Debug
builds support development, while Release builds enable optimization for performance measurements
and packaged games.

## Usage and behavior

Clone with submodules so the third party libraries come along:

```bash
git clone --recursive https://github.com/kennynguyen216/FirstPrinciples.git
```

In an existing clone run `git submodule update --init`.

## macOS

```bash
Scripts/Setup.sh
Scripts/Build.sh
```

`Setup.sh` installs premake, SDL3, Qt and 7-Zip with Homebrew, then downloads the Ultralight SDK
used for game UI (`Scripts/GetUltralight.sh` does just that part). `Build.sh` builds the Sample
project: makefiles go in `Projects/Sample/Build` and everything builds into
`Projects/Sample/Binaries/Debug`. Pass `release` for an optimised build, and a project folder to
build another one, like `Scripts/Build.sh debug Projects/MyGame`. For Xcode run
`Scripts/GenerateXcode.sh` instead.

If an incremental build reports missing symbols after a merge or branch switch, run
`Scripts/Build.sh debug Projects/Sample --clean`. This removes cached output for the selected
configuration before rebuilding. Source files and content are kept.

## Windows

1. Install Visual Studio 2022 or 2026 (the IDE or Build Tools) with the "Desktop development with C++" workload.
2. Install Qt 6 with the [Qt online installer](https://www.qt.io/download-qt-installer) and pick the
   `MSVC 2022 64-bit` kit.
3. MSVC 64-bit kits under `C:\Qt` are detected automatically. For another location, set `QT_DIR`
   to the kit directory and reopen your terminal. MinGW kits cannot link with this MSVC build.
4. Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Setup.ps1
Scripts\GenerateProjects.bat
```

5. Open the generated solution in `Projects\Sample\Build` (`Sample.sln` for VS 2022 or
   `Sample.slnx` for VS 2026), set `Editor` as the startup project and build.
   Pass a project folder to generate another one, like `Scripts\GenerateProjects.bat Projects\MyGame`.

`GenerateProjects.bat` selects the installed VS 2022 or VS 2026 C++ toolset. With Build Tools only,
run `msbuild Projects\Sample\Build\Sample.slnx /m /p:Configuration=Debug /p:Platform=x64` from its
Developer PowerShell or Developer Command Prompt (use `.sln` for VS 2022). Use
`Configuration=Release` for an optimised build. Run `powershell -ExecutionPolicy Bypass -File
Scripts\TestGenerateProjects.ps1` to check Windows project generation.

## Build targets

| Target | What it is |
|---|---|
| Engine | Static lib with `src/HAL`, `src/Core`, `src/CoreObject` and `src/Engine` |
| ImGui | Dear ImGui core, demo and SDL_GPU renderer backend from the docking branch |
| Game | The project's components and systems (`Source`) |
| Editor | Qt editor |
| Runtime | Standalone player for a scene |
| AssetTool | Imports metadata, validates assets and lists dependencies |
| PackageTool | Validates assets, cooks content and stages a Release game for distribution |
| GameScripts | Hot reloadable C++ scripts (`Source/Scripts`) |
| Benchmarks | Repeatable CPU benchmarks. Run the Release executable |
| Tests | Unit tests. Run `Projects/Sample/Binaries/Debug/Tests [filter]` |
| EditorTests | Input actions editor tests. Run `Projects/Sample/Binaries/Debug/EditorTests` |

## How it works

Premake reads the selected .fpproject and links its Source into the editor and runtime. Generated
makefiles or Visual Studio solutions and their outputs live under that project rather than the
engine source tree. The shader binaries are committed separately, so ordinary game builds do not
need shader compilers.

Source: [premake5.lua](../premake5.lua), [Build.sh](../Scripts/Build.sh),
[GenerateProjects.bat](../Scripts/GenerateProjects.bat).

## Related guides

[Project structure](projects.md), [Packaging](packaging.md), [Shader compilation](rendering.md),
[Testing](testing.md).
