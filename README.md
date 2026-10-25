# FirstPrinciples

FirstPrinciples is a C++20 game engine with a Qt scene editor and an SDL3 runtime for macOS and
Windows. It includes an entity component system, hot-reloadable C++ scripts, 2D and 3D rendering,
physics, animation, audio, game UI and peer-to-peer multiplayer.

AI was heavily used in all systems except the core memory and fiber systems.

Game projects live under `Projects/` and share the engine in `src/` and `Engine/Content/`.
DeadAirFreight is the game under active development in `Projects/DeadAirFreight`.
`Projects/Sample` contains example scenes and scripts for trying the features.

## Get the source

Clone with submodules:

```bash
git clone --recursive https://github.com/kennynguyen216/FirstPrinciples.git
cd FirstPrinciples
```

Run the build and tool commands below from the repository root.

For an existing clone, initialize dependencies with `git submodule update --init`.

## Build and run

### macOS

Install Homebrew, then run:

```bash
Scripts/Setup.sh
Scripts/Build.sh
Projects/Sample/Binaries/Debug/Editor
```

Setup installs Premake, SDL3, Qt and 7-Zip, and downloads the optional Ultralight game UI SDK.
Use `Scripts/Build.sh release` for an optimized build. Pass a project folder to build another
game, such as `Scripts/Build.sh debug Projects/DeadAirFreight`.

### Windows

1. Install Visual Studio 2022 or 2026 with Desktop development with C++.
2. Install a 64-bit MSVC Qt 6 kit. Kits under `C:\Qt` are detected automatically. For another
   location, set `QT_DIR` to the kit directory. MinGW Qt kits are not compatible with this build.
3. From the repository root, run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Setup.ps1
Scripts\GenerateProjects.bat
```

Open the generated solution in `Projects\Sample\Build`, choose Debug or Release, set Editor as
the startup project and build. The generator selects the installed Visual Studio C++ toolset.

See [building](docs/building.md) for Xcode, command-line MSBuild, clean builds and build targets.
See [packaging](docs/packaging.md) to create a distributable game.

## First steps

Open a Sample scene in the editor and press Play, or launch the standalone player:

```bash
Projects/Sample/Binaries/Debug/Runtime
```

On Windows, use `Runtime.exe` from the same configuration's output folder.
Start with [projects](docs/projects.md), [the editor](docs/editor.md) and
[components and systems](docs/gameplay.md) when making your own game.

## Documentation

The [documentation index](docs/README.md) describes each guide. Feature docs explain what the
system does, why it exists, how it works and where to find its implementation.

- Development: [projects](docs/projects.md), [components and systems](docs/gameplay.md),
  [C++ scripts](docs/scripting.md), [input](docs/input.md), [save data](docs/save-data.md).
- Editor and content: [editor controls](docs/editor.md), [hierarchy](docs/hierarchy.md),
  [assets](docs/assets.md), [undo and redo](docs/undo-redo.md), [logging](docs/logging.md).
- Graphics: [rendering](docs/rendering.md), [meshes and cameras](docs/meshes-and-cameras.md),
  [materials](docs/materials.md), [shadows](docs/shadows.md), [environment lighting](docs/environment.md),
  [animation](docs/animation.md), [particles](docs/particles.md), [post-processing](docs/post-processing.md),
  [batching](docs/batching.md), [meshlets](docs/meshlets.md), [black holes](docs/black-holes.md).
- Gameplay and sound: [physics](docs/physics.md), [game UI](docs/game-ui.md),
  [audio](docs/audio.md), [spatial audio](docs/spatial-audio.md), [audio demos](docs/audio-demos.md),
  [multiplayer](docs/multiplayer.md), [voice chat](docs/voice-chat.md).
- Engine internals: [core utilities](docs/core.md), [memory](docs/memory.md), [fibers](docs/fibers.md),
  [jobs](docs/jobs.md), [object lifetime](docs/objects.md).
- Verification and reference: [tests](docs/testing.md), [benchmarks](docs/benchmarks.md),
  [dependencies](docs/third-party.md), [current limitations](docs/limitations.md).

## Tests

After building Sample, run:

```bash
Projects/Sample/Binaries/Debug/Tests
```

Use `Tests.exe` on Windows. An optional substring filters cases, such as `Tests Memory`.
See [testing](docs/testing.md) for GPU checks, editor tests and screenshot smoke runs.
