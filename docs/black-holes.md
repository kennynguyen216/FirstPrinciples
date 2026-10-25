# Black holes and gravitational lensing

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Black holes provide a visible lensing effect and a gravity field for game bodies. They support space
scenes and gravity-assist gameplay with an accretion disk and curved light paths.

## Usage and behavior

A BlackHole bends light like the real thing. Light rays are traced backwards through Schwarzschild
spacetime, so you get the black shadow at 2.6 times the horizon radius, the photon ring, the far
side of the accretion disk lensed up over the top and a thinner image wrapped underneath. A skybox
is bent all the way around. Without one, procedural stars fill the sky.

The expensive ray tracing uses at most about 230,400 rays per frame (640 x 360 at 16:9), independent
of the viewport resolution. A full-resolution pass reconstructs the scene using the traced
distortion, keeping foreground textures sharp. The disk and photon ring are filtered from the ray
grid, so their finest details soften at large resolutions. Steps stay small near the photon sphere
and grow farther away. To measure complete renderer frame times with GPU completion waits, run
`FP_BLACK_HOLE_BENCHMARK=1 Projects/DeadAirFreight/Binaries/Debug/Tests BlackHole_Render` on macOS
(replace the project folder for another build). This includes a four-hole case with procedural
stars.

- `SchwarzschildRadius` is the event horizon in world units. `SpeedOfLight` is a game-scale c in world
  units per second. Lower it and relativity shows up at slower speeds, and the disk spins faster.
- The disk lies in the transform's local XY plane, so rotate the entity to tilt it. It starts at the
  innermost stable orbit (3 radii) and runs to `DiskOuterRadius`. Each ring glows as a blackbody at
  its own temperature, shifted by Doppler and gravitational redshift, so the side spinning toward
  you is brighter and bluer. `DiskTemperature` sets the hottest ring (4500 K glows orange, 9000 K
  white-blue).
- Up to four holes render at once. Moving a hole is fine.

A GravityBody is pulled by every black hole with the exact Schwarzschild orbit equation, so orbits
precess, nothing stays in orbit inside 3 radii and anything under 1.5 radii spirals in. `Captured`
turns on when it crosses a horizon, for your game to respawn it or end the race. With a Dynamic
RigidBody the pull goes through physics (set `GravityScale` to 0 in space). Without one, `Velocity`
moves the transform. `Thrust` lets the Move action push it.

A still black hole bends a racer's path but hands back the same speed it came in with. Speed comes
from thrusting at the closest pass, where you are fastest, or from swinging behind a moving hole.
`Content/Scenes/BlackHole.scene` has a racer to fly around one with WASD.

## How it works

BlackHoleSystem updates GravityBody motion. The renderer separately traces light through the black
hole field on a bounded grid and reconstructs scene color at full resolution. The disk’s emission
accounts for temperature and frequency shifts, and up to four holes contribute to rendering.

Source: [BlackHoleSystem.h](../src/Engine/Physics/BlackHoleSystem.h),
[Renderer.cpp](../src/Engine/Render/Renderer.cpp),
[BlackHole.frag](../src/Engine/Shaders/BlackHole.frag),
[BlackHoleComposite.frag](../src/Engine/Shaders/BlackHoleComposite.frag).

## Related guides

[Physics](physics.md), [Rendering](rendering.md), [Environment lighting](environment.md).
