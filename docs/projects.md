# Projects and content layout

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Each game has its own source, content, settings and build outputs. This allows several games to
share one engine checkout while keeping each game’s assets and compiled code together.

## Usage and behavior

Each game is a project in its own folder under `Projects`, like Unreal's. A project only sees its
own files.

```
Projects/MyGame/
  MyGame.fpproject   name and start scene
  Content/           scenes, materials and textures
  Source/            game code (Game.h, Game.cpp and your components and systems)
  Source/Scripts/    hot reloadable C++ scripts
  Build/ Binaries/ Intermediate/   generated, not committed
  Saved/Logs/        Editor.log and Runtime.log, not committed
```

Engine shaders live in `Engine/Content` and every project shares them.

File > New Project in the editor creates a project next to the open one, with an empty scene and
stub game code. File > Open Project opens another one. Game code is compiled into the editor, so
opening a project builds that project's editor and switches to it. The first build of a project
compiles the engine too and takes a little while. The Output Log shows its progress.

Each built editor or runtime finds its project by looking for a `.fpproject` file above the
executable.

## How it works

A .fpproject descriptor identifies the project name and start scene. Paths resolves project content
and engine content separately, and the build scripts use the selected project to generate and link
its Game and GameScripts targets.

Source: [Project.h](../src/Engine/Project.h), [Paths.h](../src/Engine/Paths.h),
[premake5.lua](../premake5.lua).

## Related guides

[Building](building.md), [Components and systems](gameplay.md), [Asset pipeline](assets.md).
