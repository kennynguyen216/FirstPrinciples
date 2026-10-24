# FirstPrinciples

A small C++20 game engine with an ECS, a Qt editor and SDL3 rendering. Runs on macOS and Windows.

## Setup

### macOS

```bash
Scripts/Setup.sh
Scripts/Build.sh
```

`Setup.sh` installs premake, SDL3 and Qt with Homebrew. `Build.sh` generates makefiles in `Build/`
and builds everything into `Binaries/Debug`. Pass `release` for an optimised build.
For Xcode run `Scripts/GenerateXcode.sh` instead.

### Windows

1. Install Visual Studio 2022 with the "Desktop development with C++" workload.
2. Install Qt 6 with the [Qt online installer](https://www.qt.io/download-qt-installer) and pick the
   `MSVC 2022 64-bit` kit.
3. Set `QT_DIR` to that kit, for example `C:\Qt\6.8.3\msvc2022_64`, and reopen your terminal.
4. Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Setup.ps1
Scripts\GenerateProjects.bat
```

5. Open `Build\FirstPrinciples.sln`, set `Editor` as the startup project and build.

## Projects

| Project | What it is |
|---|---|
| Engine | Static lib with `src/HAL`, `src/Core`, `src/CoreObject` and `src/Engine` |
| Game | The game's components and systems (`Game/`) |
| Editor | Qt editor |
| Runtime | Standalone player for a scene |
| Tests | Unit tests. Run `Binaries/Debug/Tests [filter]` |
