# Particle effects

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Particle emitters create short-lived visual detail such as smoke, fire, sparks and dust without
requiring one entity for every visible particle. Parameters can be previewed in the editor.

## Usage and behavior

A ParticleEmitter sprays small camera facing quads for fire, smoke, sparks, dust and magic. Entity >
Create Particle Emitter (or Particle Emitter in Place Entities) adds one, and emitters run in the
editor too, so effects can be tuned without playing.

- `Rate` streams particles every second and `Burst` fires a batch when play starts.
  `MaxParticles` caps how many are alive at once.
- Each particle lives for `Lifetime` seconds, leaving from a `Point`, `Sphere` or `Box` along
  `Direction`, within `Spread` degrees of it, at `Speed`. The random settings vary each one.
- `Gravity` pulls them down (1 is normal, negative rises) and `Drag` slows them.
- Size and color fade from `StartSize` and `StartColor` to `EndSize` and `EndColor`. An alpha of
  zero at the end fades them out.
- With no `Texture` each particle is a soft dot. `Additive` blending glows, `Alpha` covers, which
  suits smoke.
- `WorldSpace` leaves particles behind as the emitter moves. Turn it off for trails that move
  with it.
- `DestroyWhenDone` removes the entity once it stops emitting and its particles are gone, for one
  off effects:

```cpp
const Entity blast = Spawn("Sparks", hit.Point);
ParticleEmitterComponent& sparks = GetWorld().Add<ParticleEmitterComponent>(blast);
sparks.Rate = 0;
sparks.Burst = 80;
sparks.Spread = 180;
sparks.DestroyWhenDone = true;
```

`Content/Scenes/Particles.scene` has a campfire with smoke, a fountain and a ball of sparkles.

## How it works

ParticleSystem tracks emitter state and advances particle lifetime, motion, size and color. The
renderer uploads camera-facing quads for live particles and groups them by texture and blending
mode. Particles are drawn before glass so glass can refract them.

Source: [ParticleSystem.h](../src/Engine/Particles/ParticleSystem.h),
[Renderer.cpp](../src/Engine/Render/Renderer.cpp).

## Related guides

[Burst effects from scripts](scripting.md), [Materials](materials.md), [Rendering](rendering.md).
