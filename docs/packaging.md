# Packaging games

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Packaging produces a distributable game containing the standalone runtime, game scripts, required
libraries and cooked content. It gives the installed game its own content location while retaining a
writable save location.

## Usage and behavior

Use File > Package Game > macOS (Release) or Windows (Release) in the editor and choose an output
folder. The target for the other operating system is disabled. Build on macOS for a macOS app and on
Windows for a Windows game. Packaging needs the same compiler, Premake and Qt setup as building the
editor. The first Release build can take a few minutes. Progress and errors appear in the Output
Log, and the editor shows the result when packaging finishes.

The build uses the project's `StartScene`. Save scene, material and script changes before packaging.
The editor offers to save unsaved scenes and scripts. Packaging builds the Release runtime and
`GameScripts`, then puts them together with their required libraries and Ultralight resources. macOS
produces `<Project>.app` and Windows produces a `<Project>-Windows` folder with `Runtime.exe`. The
output must be new so an earlier build is never overwritten. Copy the whole app or folder to another
machine of the same operating system and architecture.

Project `Content` and `Engine/Content` are cooked into `Content.pak`. The cooker stores all files,
including model buffers, textures, audio, scenes, materials, web pages and compiled shaders, with
relative paths and per-file checksums. It preserves their existing runtime formats and does not
compress them or remove unreferenced assets. Symlinks, unsafe paths and names that differ only by
ASCII case are rejected. Game code is linked into the runtime and C++ scripts go into `GameScripts`.
Source code, build tools and editor binaries are left out of the package.

At launch the runtime verifies and unpacks the archive into a private directory under the user's
application data folder. All asset loaders and scripts use that content directory, which is removed
on normal exit. Use `Paths::GetSavedDir()` for writable saved data outside the app. Runtime logs go
there too. Each running game gets its own content directory. This also lets audio streaming, glTF
external files and relative web resources use their normal file access. A corrupt archive or a
missing script library stops a packaged game at startup.

The macOS packager copies the library dependency tree, rewrites library paths and applies local ad
hoc signatures. Public distribution still needs your own signing and notarization. Windows collects
imported DLLs and the installed x64 MSVC redistributable libraries. Qt is used by the packaging tool
and is not included in the game.

After generating projects, packaging is also available from the command line. Build `Runtime` and
`PackageTool` in Release first, then run:

```bash
Projects/Sample/Binaries/Release/PackageTool "$PWD" "$PWD/Projects/Sample" /path/to/Sample.app
```

On Windows use `PackageTool.exe` and a new output folder instead of an `.app` path. Run `Tests Pak`
for archive format, corruption and path validation tests.

## How it works

The editor invokes the project build and PackageTool. Assets are validated before the cooker writes
Content.pak, and the runtime verifies that archive before loading the game. Packaging uses the saved
start scene and the current platform toolchain.

Source: [Main.cpp](../src/Tools/PackageTool/Main.cpp), [Pak.h](../src/Core/Misc/Pak.h),
[Main.cpp](../src/Runtime/Main.cpp).

## Related guides

[Building](building.md), [Asset pipeline](assets.md), [Save data](save-data.md).
