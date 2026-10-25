# Input actions, gamepads and rebinding

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Named actions let gameplay code describe intent, such as Jump or Move, independently of the physical
control. Project bindings provide defaults and player overrides allow rebinding without editing
shipped content.

## Input actions and gamepads

Choose Edit > Input Actions... or double click `Input.actions` in the Content Browser to edit
bindings in a tab. Each row has an action name, device, input, optional direction and scale. Use Add
Binding and Remove Selected to change the rows. The input picker offers the names accepted by each
device. Leave a scale blank to keep its default behavior, especially for sticks.

The Source view edits the same document and preserves comments. Undo and redo work in both views.
Save with Ctrl+S, the tab's Save button or Save All. Invalid lines are listed below the bindings and
cannot be saved. Click an error to jump to its source. Unsaved edits are checked when closing a tab,
quitting or packaging. Changes apply the next time Play starts and when the runtime next opens the
project. Opening the editor from the Edit menu recreates a missing `Input.actions` with the
defaults.

Gameplay scripts can also change bindings immediately through `Input`:

```cpp
const auto names = Input::GetActionNames();
const auto bindings = Input::GetActionBindings("Jump");
Input::RebindAction("Jump", 0, "Key:G");
Input::SetActionBindings("Jump", { "Key:G", "Button:South" });
Input::SetActionBindings("Jump", {}); // leave the action unbound
Input::ResetActionBindings("Jump");
Input::ResetAllActionBindings();
```

Replacement strings use the same device, direction and scale syntax as `Input.actions`. Invalid
replacements leave the current bindings alone. Reset restores the bindings from the most recent
`LoadActions` or `LoadActionsFile`, so project defaults stay separate from player preferences.

For a menu that asks the player to press a new control, call `Input::BeginActionRebind("Jump", 0)`,
then check `Input::GetActionRebindStatus()` for `Listening`, `Complete` or `Cancelled`.
`GetLastReboundInput()` returns the completed binding string. Capture accepts fresh keyboard, mouse
button and gamepad button presses. Escape or `CancelActionRebind()` cancels, and losing focus
cancels too. Held controls are ignored. Named actions are suppressed during capture and until the
captured control is released.

Pass `true` as the third argument to capture individual gamepad axes too. An axis must first return
near its center and then move past 75 percent. Capturing preserves the binding's direction and
scale, with negative axis movement inverted to match the chosen direction. Mouse motion is ignored
during capture. Use `SetActionBindings` for whole sticks or mouse delta bindings.

Use `Input::SaveActionBindings()` to save player overrides and `Input::LoadActionBindings()` at play
start to restore them. Both accept an optional SaveData slot name for profiles. These files live in
the project's user save folder and leave `Content/Input.actions` unchanged. Only changed actions are
saved, so new project actions remain available. Failed loads leave current bindings alone. Saving
after a reset keeps the reset for the next launch.

Open `Scenes/InputBindings.scene` and press Play for a working menu, backed by the `InputRebindMenu`
script and `UI/InputBindings.html`. It lists bindings, captures replacements, accepts typed bindings
and saves or restores controls through the existing `engine.send` bridge. The Jump counter lets you
try the new binding after releasing its capture press. The menu uses the same optional Ultralight
SDK as other game UI pages.

Games ask for actions like Jump or Move instead of keys, so the keyboard, mouse and any gamepad all
play. `Content/Input.actions` binds them, one input per line, and new projects get a copy of the
defaults:

```
Jump   Key:Space
Jump   Button:South          # A on Xbox, Cross on PlayStation
Move   Key:W        +Y       # keys point a 2D action, right and up positive
Move   Stick:Left
Look   Mouse:Delta
Look   Stick:Right  800      # a scale makes a stick turn at a steady speed
Fire   Axis:RightTrigger
```

Devices are `Key` (SDL key names with `_` for spaces), `Mouse` (`Left`, `Right`, `Middle`, `Delta`),
`Button` and `Axis` for gamepads (named by position, like `South`, `LeftShoulder`, `DpadUp` or
`RightTrigger`) and `Stick` (`Left` or `Right`). The defaults bind Move, Look, Jump, Run, Fire, Aim,
Interact and Pause. The editor reloads the file each time Play starts.

```cpp
if (Input::WasActionPressed("Jump"))
    Jump(6);
const Vec2 move = Input::GetActionAxis("Move"); // keys and the left stick
if (Input::IsActionDown("Fire"))
    Input::Rumble(0.3f, 0.6f, 0.1f);
```

Every connected pad counts, so whichever one is picked up plays. Sticks have a round dead zone so
they read zero at rest.

## Mouse and cameras

While the game plays the mouse is hidden and held in place, so `Input::GetMouseDelta()` drives mouse
look. Call `Input::SetCursorMode(CursorMode::Visible)` in `OnStart` for menus and HUDs with buttons,
or `Hidden` to draw your own pointer. In the editor Esc frees the mouse and a click on the viewport
takes it back.

The game sees through the first camera with `Primary` on. `SetActiveCamera` cuts to another one by
turning `Primary` off everywhere else.

## How it works

Input translates SDL keyboard, mouse and gamepad events into raw state and named actions. The
actions file and in-game rebinding use the same binding syntax. The editor’s table and Source views
share one document, and player overrides are serialized separately through SaveData.

Source: [Input.h](../src/Engine/Input.h),
[InputActionsEditor.cpp](../src/Editor/InputActionsEditor.cpp).

## Related guides

[Scripts](scripting.md), [Save data](save-data.md), [Cameras](meshes-and-cameras.md), [Rebinding
UI](game-ui.md).
