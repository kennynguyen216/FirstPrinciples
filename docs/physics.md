# Physics and characters

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Physics components give scene objects collisions, rigid body motion and character movement. They
connect game entities to Jolt so gameplay can apply forces, ask collision questions and react to
contacts.

## Usage and behavior

Physics runs on [Jolt](https://github.com/jrouwe/JoltPhysics) while the game plays.

- A Collider gives an entity a shape: `Box` (`Size`), `Sphere` (`Radius`), `Capsule` (`Radius` and
  `Height`, standing along Y) or `Mesh` (the entity's own mesh). Sizes scale with the transform and
  `Offset` moves the shape. On its own a collider is a solid, unmoving part of the level.
- A RigidBody makes it move. `Dynamic` bodies fall, bounce and get pushed. `Kinematic` bodies
  follow their transform, so scripts or animation move them, and they shove dynamic bodies aside.
  `Static` never moves. `Mass` is in kilograms, `Restitution` is bounciness, `LockRotation` keeps
  characters upright and `Continuous` stops fast, small bodies tunnelling through thin walls.
- A collider with `Trigger` on reports overlaps instead of blocking.
- Moving a dynamic body's transform by hand teleports it. Changing its collider or rigid body
  rebuilds it. Dynamic mesh colliders use the mesh's convex hull, static ones use every triangle.
- Physics steps at a fixed 60 times a second whatever the frame rate. Children of other entities
  work too, and their bodies sit at their world position.
- The editor outlines colliders in green, and triggers in amber.

Scripts push bodies and hear about contacts:

```cpp
void OnUpdate(float) override
{
    if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
        AddImpulse({ 0, 5, 0 });
    RaycastHit hit;
    if (Raycast(GetWorldPosition(), { 0, -1, 0 }, 1.1f, hit))
        FP_LOG("Standing on %s", GetName(hit.Entity).CStr());
}

void OnCollisionEnter(Entity other, const Collision& collision) override { /* collision.Point, collision.Normal */ }
void OnTriggerEnter(Entity other) override { ++Score; }
```

`GetPhysics()` also has `AddTorque`, angular velocity and gravity, and takes any entity, like
`GetPhysics().AddImpulseAtPoint(hit.Entity, push, hit.Point)` to shove what a ray hit.
`Content/Scenes/Physics.scene` drops balls down a ramp, through a goal that counts them, and into a
pyramid of crates. Space kicks every ball. C switches to a first person camera where the mouse
looks, WASD flies, left click pushes, F picks things up and right click blows them up.

## Characters

A CharacterController makes a walking capsule for players and enemies, used instead of a collider
and rigid body. It slides along walls, climbs steps up to `StepHeight`, walks up slopes up to
`MaxSlope`, rides moving platforms and pushes crates. Rays and triggers find it like any body.

Entity > Create Character (or Character in Place Entities) adds a pill with a follow camera that
plays with no code in any project. Its PlayerInput component plays it from the Move, Look, Run and
Jump actions: WASD or the left stick walks, Shift runs, Space or South jumps and the mouse or right
stick turns it. Remove PlayerInput to drive it from a script:

```cpp
void OnUpdate(float) override
{
    const Vec2 axis = Input::GetActionAxis("Move");
    Move(Vec3(axis.X, 0, -axis.Y) * 5); // meters per second, call it every frame
    if (Input::WasActionPressed("Jump"))
        Jump(6);                        // only works on the ground
}
```

`AirControl` sets how much `Move` steers mid jump. `SetVelocity` and `AddImpulse` knock a character
about, and explosions push it too. `Content/Scenes/Character.scene` has a third person player with
stairs, a walkable and a too steep ramp, a lift, crates and a zone that counts the player walking
in.

## How it works

PhysicsSystem synchronizes scene transforms and body state with PhysicsWorld. Component settings
describe bodies, shapes and character behavior, while scripts access forces, queries and events
through the Physics facade. PlayerInputSystem supplies the built-in character’s input-driven
movement.

Source: [Physics.h](../src/Engine/Physics/Physics.h),
[PhysicsSystem.h](../src/Engine/Physics/PhysicsSystem.h),
[PhysicsWorld.h](../src/Engine/Physics/PhysicsWorld.h).

## Related guides

[World and systems](gameplay.md), [Scripts](scripting.md), [Black hole gravity](black-holes.md).
