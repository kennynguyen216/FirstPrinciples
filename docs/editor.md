# Editor layout, content browser and controls

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The editor combines scene assembly, property editing, script authoring and diagnostics in one
workspace. Dockable panels let users keep the tools needed for the current task visible.

## Usage and behavior

The editor follows Unreal Engine 5's layout and dark theme, using this engine's own terms.

- Place Entities on the left: click an entry to create it, or drag it into the viewport to place it
  under the mouse. The icons on top filter by category.
- Viewport in the middle, in a tab named after the open scene. The pills over it switch between
  Perspective and Top (2D), pick Lit, Unlit or Wireframe, and turn grid snap on with a grid size.
- Details and Hierarchy share the panel on the right as tabs. Details shows the selection's components in collapsible sections. Add components with the Add
  button, remove one with the x on its header, and filter fields with the search box.
- Hierarchy lists every entity nested under its parent with its type, plus a search box. Double-click
  an object or right-click Rename to edit its name inline. Enter commits and Escape cancels;
  renames support undo and redo. The scene row at the top holds scene scripts.
- Drag a panel by its title to move it, or drop it on another panel to share a spot as tabs. Pull a tab
  out to separate it again. Window reopens closed panels and Reset Layout puts everything back.
- Content Browser, Output Log and Profiler share the bottom. The profiler shows each
  system's last and smoothed average CPU update time, plus the total game update time. Rendering and
  editor UI time are not included. The status bar opens each panel and has a console box: type
  `help` for commands including play, pause, resume, step and stop.

The Hierarchy tab is active when the editor opens. Selecting an object there updates Details without
switching tabs. Picking an object in the viewport or selecting an asset brings Details forward so
its properties can be edited.

## Content browser

The Content Browser tab next to the Output Log at the bottom of the editor shows everything in the
project's `Content` plus its scripts in `Source/Scripts`. Textures show image thumbnails with a
checkerboard behind transparency. Materials show a lit sphere, and OBJ, glTF and GLB meshes show a
fitted preview in their bind pose. Previews fill in as you browse, with type icons for unreadable
assets. Changes to assets or their texture and buffer files refresh the thumbnails automatically.

- Double click a scene to open it or a script to edit it in a tab. Click a material to edit it in
  the Details panel, or a texture to preview it.
- Drag a texture into the viewport to make a sprite, an `.obj` to make a mesh, or a material onto a
  sprite or mesh to assign it. Drag any asset onto a matching Details field to fill it in.
- Add or a right click makes a New Folder, Material, Scene or Script. Import copies files in,
  and Save All saves the scene, materials and script tabs. Back, forward and the folder path
  above the files move between folders.
- Right click an asset to Rename (F2), Duplicate, Delete, Copy Path or Show in Finder or Explorer.
- Renaming keeps asset IDs and migrates old scene and material paths to IDs. Delete moves files and their metadata to the system Trash.
- The browser refreshes on its own when files change, and the Output Log comes forward when a script
  build fails.

## Controls

| Action | Control |
|---|---|
| Fly | Hold the right button, then WASD to move, Q and E for down and up, Shift to go faster. The wheel sets the speed while flying |
| Look around (3D) | Right drag |
| Pan | 2D: right or middle drag. 3D: middle drag or Shift right drag |
| Zoom | Mouse wheel |
| Orbit (3D) | Alt left drag |
| Toggle 3D view | Ctrl+3 or the Perspective pill. The editor opens in 3D |
| View mode | Viewport pill, or Alt+4 Lit, Alt+3 Unlit, Alt+2 Wireframe |
| Rename / delete asset | F2 / Delete in the content browser |
| Focus selection | F |
| Select / move | Left click / left drag along the view plane |
| Gizmo | Q select, W move, E rotate, R scale, or the buttons over the viewport. Drag an arrow, ring, box or plane square |
| World or local axes | Globe button next to the gizmo buttons |
| Grid snap | Grid pill over the viewport. Snaps moves to the grid and turns to 15 degrees. Shift flips snapping while dragging |
| Create sprite | Ctrl+Shift+A |
| Copy / paste / duplicate | Ctrl+C / Ctrl+V / Ctrl+D, children included |
| Delete | Delete or Backspace |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Play | Ctrl+P |
| Pause / resume | Ctrl+Shift+P or the toolbar button. The game camera stays visible |
| Step one frame | F10 while paused |
| System profiler | Profiler button in the status bar or Window > Profiler |
| New script / reload scripts | Scripts menu / Ctrl+R |
| Save | Ctrl+S |

## How it works

EditorWindow coordinates a Qt workspace around the scene World. Panels emit edit requests that
update scene data, refresh related views and enter history. The viewport uses the engine renderer
and reads its output back for Qt presentation. The system profiler measures game system updates
rather than total rendering cost.

Source: [EditorWindow.h](../src/Editor/EditorWindow.h),
[HierarchyPanel.h](../src/Editor/HierarchyPanel.h),
[ContentBrowser.h](../src/Editor/ContentBrowser.h).

## Related guides

[Renaming and parenting](hierarchy.md), [Asset pipeline](assets.md), [Input editor](input.md),
[Visual settings](post-processing.md), [History](undo-redo.md).
