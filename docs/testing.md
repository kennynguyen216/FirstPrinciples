# Tests and smoke checks

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Unit and GPU tests check subsystem behavior, while screenshot smoke runs exercise the editor and
runtime on sample content. They provide repeatable checks when changing a feature.

## Usage and behavior

```bash
Projects/Sample/Binaries/Debug/Tests
Projects/Sample/Binaries/Debug/Runtime --screenshot runtime.bmp
Projects/Sample/Binaries/Debug/Runtime --host 7777 --screenshot host.bmp
Projects/Sample/Binaries/Debug/Runtime Scenes/Physics.scene --wait 5 --screenshot physics.bmp
Projects/Sample/Binaries/Debug/Editor --play --screenshot editor.png
Projects/Sample/Binaries/Debug/Editor --scene Scenes/Lighting2D.scene --select "Glass Pane" --screenshot lit.png
Projects/Sample/Binaries/Debug/Editor --scene Scenes/Showcase3D.scene --3d --viewmode wireframe --screenshot 3d.png
Projects/Sample/Binaries/Debug/Editor --build-scripts
Projects/Sample/Binaries/Debug/Editor --browse Materials --asset Materials/Bricks.material --screenshot browser.png
Projects/Sample/Binaries/Debug/Editor --drop Textures/Bricks_AO.png --undo --redo --screenshot undo.png
Projects/Sample/Binaries/Debug/Editor --new-project MyGame
```

Run commands from the repository root after building the selected project. Tests accepts a
case-sensitive substring filter, such as Tests Memory or Tests BlackHole. Many GPU tests return
without running when no supported device is available. Use the Sample project for tests that depend
on Sample assets, including Characters/Noodle.gltf. A different game project may not contain those
fixtures.

For editor input-action checks, run Projects/Sample/Binaries/Debug/EditorTests. On Windows use the
.exe variants of the tools.

## Core subsystem checks

```bash
Projects/Sample/Binaries/Debug/Tests Memory
Projects/Sample/Binaries/Debug/Tests Fiber
Projects/Sample/Binaries/Debug/Tests Job_
Projects/Sample/Binaries/Debug/Tests Object_
Projects/Sample/Binaries/Debug/Tests GC_
```

## How it works

Tests registers cases through the small TestFramework and reports failed expectations with source
locations. GPU tests render to offscreen targets and inspect results where a device is available.
Runtime and editor command-line flags drive scene selection, play state and screenshot output for
manual comparison.

Source: [TestFramework.h](../src/Tests/TestFramework.h), [TestMain.cpp](../src/Tests/TestMain.cpp),
[Main.cpp](../src/Editor/Main.cpp), [Main.cpp](../src/Runtime/Main.cpp).

## Related guides

[Build targets](building.md), [Performance measurements](benchmarks.md), [Audio
comparisons](audio-demos.md).
