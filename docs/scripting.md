# C++ scripts and hot reload

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Scripts attach per-entity behavior written in C++ without rebuilding and restarting the whole editor
for each change. Reflection exposes selected fields and function buttons so behavior can be
configured in scenes.

## Usage and behavior

Scripts are per entity behaviour written in C++ in the project's `Source/Scripts`. They build into a
separate `GameScripts` library that the editor reloads while it runs, so you can change code without
restarting, even in the middle of play.

```cpp
#include "Engine/Scripting/Script.h"

using namespace FP;

// Turns the entity every frame. This comment shows in the editor.
FPCLASS(Category = "Movement")
class Rotator : public Script
{
public:
    FPPROPERTY(Range = "0, 720")
    float Speed = 90; // degrees per second, shown as the tooltip

    FPFUNCTION()
    void Reverse() { Speed = -Speed; }

    void OnUpdate(float deltaTime) override
    {
        GetTransform().Rotation.Y += Speed * deltaTime;
    }
};

FP_SCRIPT(Rotator);
```

## Making and attaching scripts

- In the Details panel, Add > Script lists every script, grouped by category, with
  New Script... at the bottom. A new script opens in a tab and attaches itself to the entity as
  soon as it compiles. Scripts > New Script... and the Content Browser make one without attaching it.
- Drag a script from the Content Browser onto an entity in the viewport to attach it.
- An entity can have any number of scripts. Each shows what it does (the comment above the class),
  which callbacks it runs, its fields and its function buttons.
- Saving a script rebuilds and reloads it (turn this off with Scripts > Auto Reload).
  Scripts > Reload Scripts (Ctrl+R) forces it. Build output goes to the console, and double
  clicking an error opens the file at that line.

## Reflection

A header tool, like Unreal's, reads the scripts before every build and generates the reflection that
`FP_SCRIPT` registers. There is nothing to list by hand.

- `FPPROPERTY` exposes a field. It shows in the Details panel, saves with the scene and keeps its
  value across hot reloads. Other members reset when scripts reload. Options: `Tooltip = "..."`,
  `Category = "..."` (fields are grouped under it), `Range = "min, max"` (clamps, and short ranges
  get a slider), `ReadOnly` and `Asset = "Scenes (*.scene)"` (a file picker). A comment on the line
  above or at the end of the line becomes the tooltip.
- `FPFUNCTION` on a function with no arguments adds a button that calls it while playing.
- `FPCLASS` is optional and takes `Category` and `Description`. Without a description, the comment
  right above the class is used.
- Marked members must be public, or put `FP_BODY()` in the class to reflect private ones.
- Mistakes such as a missing declaration, a function with arguments or a private field without
  `FP_BODY()` fail the build with the file and line, like a compiler error.
- Field types: `bool`, `int32`, `float`, `Vec2`, `Vec3`, `Color` and `String`.
- Code the tool does not see can list fields by hand with
  `FP_SCRIPT_FIELDS(Name) { fields.Field("Speed", &Name::Speed).Range(0, 10); }`.

## Helpers

| | |
|---|---|
| Callbacks | `OnStart`, `OnUpdate(deltaTime)`, `OnStop`, `OnHotReload` (calls `OnStart` by default) |
| This entity | `GetEntity()`, `GetTransform()`, `Get<T>()`, `TryGet<T>()`, `Has<T>()`, `Add<T>()`, `Remove<T>()`, `GetAnimator()`, `GetTime()` |
| Other entities | `FindEntity(name)`, `FindEntities(name)`, `TryGet<T>(entity)`, `IsValid(entity)`, `GetName(entity)` |
| Other scripts | `GetScript<T>(entity)` on an entity (this one by default), `FindScript<T>()` anywhere |
| Spawning | `Spawn(name, position)`, `SpawnScene("Prefabs/Enemy.scene", position)`, `Destroy(entity)`, `DestroySelf()` |
| Scenes | `LoadScene("Scenes/Level2.scene")` switches at the end of the frame. `GetSceneEntity()` |
| Parents | `GetParent()`, `GetChildren()`, `FindChild(name, recursive)`, `SetParent(entity)`, `AddChild(entity)` |
| World space | `GetWorldPosition()`, `SetWorldPosition(position)`, `GetWorldRotation()` |
| Timers | `After(seconds, fn)`, `Every(seconds, fn)`, `CancelTimer(id)`, `CancelAllTimers()` |
| Saving | `SaveData` holds typed values. `Save(slot)`, `Load(slot)`, `Exists(slot)`, `Delete(slot)` |
| Physics | `AddForce(force)`, `AddImpulse(impulse)`, `AddForceAtPoint`, `AddImpulseAtPoint`, `GetVelocity()`, `SetVelocity(velocity)`, `GetMass()`, `GetPhysics()` for the rest |
| Characters | `Move(velocity)` every frame, `Jump(speed)`, `IsGrounded()` |
| Particles | `EmitParticles(count)` bursts from this entity's emitter, or another's |
| Physics queries | `Raycast(origin, direction, distance, hit)`, `SphereCast(origin, radius, direction, distance, hit)`, `RaycastFromCamera(distance, hit)`, `OverlapSphere(center, radius)`, `AddExplosion(center, radius, impulse)` |
| Cameras | `SetActiveCamera(entity or name)` (this entity by default), `GetActiveCamera()`, `IsActiveCamera()` |
| Physics events | `OnCollisionEnter(other, collision)`, `OnCollisionExit(other)`, `OnTriggerEnter(other)`, `OnTriggerExit(other)` |
| Math | `Clamp`, `Lerp`, `InverseLerp`, `MoveTowards`, `Damp`, `Random::Range`, `Random::Chance`, `Random::Direction` |
| Input actions | `Input::IsActionDown("Fire")`, `Input::WasActionPressed("Jump")`, `Input::GetActionValue("Fire")`, `Input::GetActionAxis("Move")` |
| Gamepads | `Input::IsGamepadButtonDown(GamepadButton::South)`, `Input::GetGamepadAxis(GamepadAxis::RightTrigger)`, `Input::GetGamepadStick(right)`, `Input::Rumble(low, high, seconds)` |
| Input and logs | `Input::IsKeyDown(SDL_SCANCODE_SPACE)`, `Input::GetMoveAxis()`, `Input::GetMouseDelta()`, `Input::SetCursorMode(mode)`, `FP_LOG("...")` |

`SpawnScene` copies every entity in a scene file with new ids, so any scene works as a prefab and
can be spawned many times. Timers stop when the script stops or reloads.

## Scene scripts

Click the scene's row at the top of the Hierarchy to edit the scene itself. Scripts added there run
for the whole scene, which suits game rules, waves of enemies or score. They live on a hidden Scene
entity that saves with the scene, so `FindScript<GameRules>()` reaches them from anywhere.

Components still live in `Source` and are registered in `Game::RegisterComponents`. Keep new
component types out of `Source/Scripts`, since data owned by the scripts library would vanish when
it reloads.

The runtime loads the same library at startup. Building from the command line works as before, and
the `GameScripts` project builds along with the editor, running the header tool first.

## How it works

HeaderTool generates reflection metadata for marked declarations. GameScripts is loaded as a
separate module, and ScriptRuntime replaces that module during reload while preserving exposed
values. ScriptSystem dispatches lifecycle callbacks and owns per-entity script instances. Engine
components stay in the non-reloadable game code.

Source: [Script.h](../src/Engine/Scripting/Script.h),
[ScriptRuntime.h](../src/Engine/Scripting/ScriptRuntime.h),
[ScriptSystem.h](../src/Engine/Scripting/ScriptSystem.h).

## Related guides

[Components and systems](gameplay.md), [Input](input.md), [Save data](save-data.md), [Game
UI](game-ui.md).
