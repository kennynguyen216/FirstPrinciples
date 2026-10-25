# Hierarchy, names and transforms

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Parenting lets a scene describe objects that move together, such as a camera carried by a player or
parts attached to a vehicle. Object names make those relationships easier to navigate in the
hierarchy.

## Usage and behavior

Drag an entity onto another in the Hierarchy to make it a child, or onto the scene row to make it
top level again. Right click for Create Child and Detach from Parent. A child's Transform is
relative to its parent, so it moves, turns and scales along with it. The gizmo still works in world
space. Deleting, copying and duplicating an entity takes its children with it.

## Renaming objects

Double-click an object in the Hierarchy, or right-click it and choose Rename, to edit its name
inline. Enter commits and Escape cancels. Blank names are rejected and surrounding whitespace is
trimmed. Renames update the Name component, mark the scene as changed and support undo and redo. The
scene row and Type column are not renamed.

## How it works

World stores each entity’s parent relationship and computes world transforms from local transforms.
The editor turns drag-and-drop operations into parenting requests. Renaming changes the Name
component and uses ordinary scene history, so IDs and parent relationships remain stable.

Source: [World.h](../src/Engine/World.h), [HierarchyPanel.cpp](../src/Editor/HierarchyPanel.cpp),
[EditorWindow.cpp](../src/Editor/EditorWindow.cpp).

## Related guides

[Entities](gameplay.md), [Undo and redo](undo-redo.md), [Editor controls](editor.md).
