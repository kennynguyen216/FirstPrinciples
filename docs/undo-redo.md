# Scene undo and redo

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Scene history lets users recover from edits and compare changes without manually reconstructing a
previous scene. Text fields and script tabs keep their own local text history.

## Usage and behavior

Ctrl+Z undoes and Ctrl+Shift+Z redoes (Cmd on macOS, and Ctrl+Y also redoes on Windows). Both are in
the Edit menu too.

- Every scene edit can be undone: moving, creating, duplicating, deleting, dropping assets, Details
  fields, components, scripts and material values. The selection comes back with each step.
- A drag or a burst of typing becomes one step once the mouse comes up and edits pause briefly.
- Script tabs and text fields keep their own text undo while they have focus.
- History holds the last 100 steps and starts over when a scene is opened. Play mode changes are not
  recorded since Stop puts the scene back anyway.

## How it works

SceneHistory captures the serialized world, loaded material text and selection after an edit. Undo
and redo restore snapshots and rebuild editor views. The editor coalesces nearby edits so dragging
or typing does not produce a history entry for every intermediate value.

Source: [SceneHistory.h](../src/Engine/SceneHistory.h),
[SceneHistory.cpp](../src/Engine/SceneHistory.cpp),
[EditorWindow.cpp](../src/Editor/EditorWindow.cpp).

## Related guides

[Editor](editor.md), [Object renaming and parenting](hierarchy.md), [Asset references](assets.md).
