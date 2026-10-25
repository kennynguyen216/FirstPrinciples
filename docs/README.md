# FirstPrinciples documentation

[Project README](../README.md)

These guides explain the purpose, usage, implementation and limits of each feature. Commands assume
the repository root as the working directory unless a guide says otherwise. Sample scene paths such
as `Scenes/Physics.scene` are relative to `Projects/Sample/Content`.

Start with [building](building.md), [project structure](projects.md) and [the editor](editor.md).
For a first gameplay feature, read [components and systems](gameplay.md), then
[scripts](scripting.md).

## Building and shipping

| Guide | What it covers |
|---|---|
| [Building](building.md) | Dependencies, macOS and Windows builds, Xcode, clean builds and build targets |
| [Projects](projects.md) | Project descriptors, source folders, content and generated output |
| [Packaging](packaging.md) | Release builds, content archives, validation and platform distribution |
| [Third-party dependencies](third-party.md) | Library roles, acquisition, version notes and SDK integration |
| [Current limitations](limitations.md) | Constraints to account for when building a game |

## Gameplay and tools

| Guide | What it covers |
|---|---|
| [Worlds, components and systems](gameplay.md) | Entity data, component registration, systems and play state |
| [C++ scripts](scripting.md) | Per-entity behavior, reflection, helpers, scene scripts and hot reload |
| [Input](input.md) | Actions, gamepads, mouse input, binding files and player rebinding |
| [Save data](save-data.md) | Typed progress and settings, slots, file locations and failure behavior |
| [Editor](editor.md) | Docking, content browser, viewport controls and system profiling |
| [Hierarchy](hierarchy.md) | Parents, children, local transforms and object renaming |
| [Assets](assets.md) | Stable IDs, .fpmeta files, dependencies, reimport and validation |
| [Undo and redo](undo-redo.md) | Scene snapshots, edit coalescing, selection and text history |
| [Physics](physics.md) | Rigid bodies, colliders, forces, queries, triggers and character movement |
| [Game UI](game-ui.md) | HTML pages, camera overlays, state binding and C++/JavaScript events |

## Rendering

| Guide | What it covers |
|---|---|
| [Render pipeline](rendering.md) | SDL_GPU backends, render stages and shader compilation |
| [Meshes and cameras](meshes-and-cameras.md) | Imported geometry, sections, projections and camera axes |
| [Materials and lighting](materials.md) | Lit, Unlit, Glass, texture maps, cel shading and direct lighting |
| [Shadows](shadows.md) | Directional, spot and point shadow maps and quality settings |
| [Environment lighting](environment.md) | Skyboxes, diffuse light, filtered reflections and reload behavior |
| [Animation](animation.md) | Skins, clips, bone limits, state machines and transitions |
| [Particles](particles.md) | Emitters, bursts, particle motion, lifetime and blending |
| [Post-processing](post-processing.md) | FXAA, bloom and per-user color assistance |
| [Batching](batching.md) | Instance data, draw compatibility, ordering and counters |
| [Meshlets](meshlets.md) | Cluster construction, frustum and occlusion culling and visibility debugging |
| [Black holes](black-holes.md) | Lensing, accretion disks, gravity bodies and rendering cost |

## Audio and networking

| Guide | What it covers |
|---|---|
| [Audio](audio.md) | Importing, voices, streaming, playback controls, buses and diagnostics |
| [Spatial audio](spatial-audio.md) | Listener placement, attenuation, acoustic materials, rooms and Doppler |
| [Audio demos](audio-demos.md) | Repeatable comparisons and scene and audio generators |
| [Multiplayer](multiplayer.md) | Peer sessions, ownership, replication, channels, encryption and limits |
| [Voice chat](voice-chat.md) | Microphone opt-in, proximity speech, Opus and network playback |

## Core internals

| Guide | What it covers |
|---|---|
| [Core utilities](core.md) | Arrays, hash maps, UTF-8 strings, IDs and ownership helpers |
| [Memory](memory.md) | Heap, page, arena, pool and size-class allocators and their lifetimes |
| [Fibers](fibers.md) | Context switching, stacks, platform implementations and the fiber pool |
| [Jobs](jobs.md) | Worker scheduling, dependency counters, nested waits and configuration |
| [Object lifetime](objects.md) | Class metadata, roots, strong and weak handles and garbage collection |
| [Logging](logging.md) | Log levels, sinks, file rotation and editor output |

## Verification

| Guide | What it covers |
|---|---|
| [Tests and smoke checks](testing.md) | Test filters, Sample fixtures, GPU checks and screenshot commands |
| [Profiling and benchmarks](benchmarks.md) | Repeatable CPU measurements, render timing and profiler scope |

When documenting a change, update the relevant feature guide and its source links. Keep the project
README focused on the project overview, build steps and navigation.
