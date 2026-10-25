# Current limitations

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The engine is still developing. These limits describe the implemented behavior so game designs and
performance expectations can account for it.

## Usage and behavior

- The editor viewport copies each rendered frame back to Qt, which adds GPU synchronization and CPU overhead.
- Some file loaders still use `fopen`, so non-ASCII paths on Windows are not supported consistently.
- Asset scans and imports run synchronously. There is no general asynchronous asset streaming or residency budget.
- Dependency discovery cannot inspect paths built dynamically in code. Relative links in model and web files constrain asset moves.
- Packaging preserves source asset formats and includes all content without compression or unused-asset removal.
- Meshlets provide visibility culling, with no hierarchical LOD selection or virtualized geometry streaming. Animated meshes retain full draws.
- Batching combines compatible neighboring draws. Material, texture and section changes split batches, and animated meshes do not instance their poses.
- Games use the built-in material shaders. Custom game shaders are not supported yet. Lighting supports up to 16 lights and one shadow caster of each supported type.
- Glass renders after opaque objects, so overlapping glass does not accumulate refraction through every surface.
- Animation state machines are authored in C++. There is no animation graph editor, blend spaces, root motion or animation event authoring.
- Scene spawning creates copies, with no linked prefab instances or overrides.
- Networking trusts participating peers, supports up to 16 peers, and has no relay for strict NATs.
- Scene undo stores full snapshots and retains up to 100 steps. Large scenes can increase its memory and serialization cost.
- The job queue uses a lock.
- Editor grid and gizmo lines draw on top of scene geometry.
- Transform rotations are Euler angles and can gimbal lock under combined rotations.

## How it works

The limits below apply to the current implementation. Feature guides explain the corresponding
controls and tradeoffs, and the source links identify the systems where those limits originate.

Source: [Renderer.cpp](../src/Engine/Render/Renderer.cpp), [World.h](../src/Engine/World.h),
[NetSession.h](../src/Engine/Net/NetSession.h), [SceneHistory.h](../src/Engine/SceneHistory.h).

## Related guides

[Rendering](rendering.md), [Assets](assets.md), [Networking](multiplayer.md), [Jobs](jobs.md).
