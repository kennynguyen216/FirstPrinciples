# Batching and mesh instancing

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Batching reduces repeated CPU draw submissions for compatible objects while retaining each object’s
transform and appearance. It is useful when many draws share geometry and materials.

## Usage and behavior

Static meshes and sprites batch automatically when neighboring draws share a mesh, section,
material, shader and texture. Each instance supplies its own transform, normal matrix, UV transform
and tint through a reusable vertex buffer. Full mesh draws use an instance count instead of one draw
per object. The shadow maps and occlusion depth prepass also instance compatible static meshes,
after checking each object's visibility for that pass.

Meshlet rendering keeps separate visibility decisions for every object's clusters. Compatible
indirect command ranges share an SDL draw submission, with each command selecting its instance data.
This reduces CPU submissions and repeated state binding while retaining cluster culling. It does not
combine those commands into a single GPU instance count or guarantee a frame time improvement.

Batching preserves the existing draw order: opaque meshes front to back, sprites by layer and depth,
then glass back to front. Incompatible neighbors split a batch. Animated meshes keep independent
bone uniforms and draws. Meshlet color debugging uses separate draws to preserve cluster colors.
Missing instance pipelines fall back to the existing object path.

`Renderer::SetBatchingEnabled(false)` provides a comparison path. `GetRenderStats()` reports the
latest view's scene SDL draw submissions, section instances, direct instanced groups, indirect
commands, shadow submissions and occlusion prepass submissions. These counts exclude particles, UI,
debug lines and fullscreen passes, and are not GPU timing measurements. The meshlet debugger
includes the batching toggle and counters.

GPU regression tests compare pixels with batching enabled and disabled. On the 12-cube test scene,
scene submissions fall from 12 to 1, the occlusion prepass from 12 to 1, and eight directional, spot
and point shadow maps from 96 to 8. Tests also cover transparency order, texture and material
splits, animated poses, movement, resizing, and multiple views recorded into one command buffer.

## How it works

Renderer builds reusable instance data and joins compatible neighboring draw ranges. Direct
instancing uses an instance count, while meshlet indirect commands retain separate visibility
decisions and select their instance data. Pass-specific checks preserve draw order, transparency and
shadow visibility.

Source: [Renderer.cpp](../src/Engine/Render/Renderer.cpp),
[RenderBatchTests.cpp](../src/Tests/RenderBatchTests.cpp).

## Related guides

[Render pipeline](rendering.md), [Meshlet culling](meshlets.md), [Measurement](benchmarks.md).
