# Meshes and cameras

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Mesh components draw reusable geometry, while cameras define how world space is projected into an
image. Material sections let a single model contain surfaces with different shaders.

## Usage and behavior

Add a Mesh component to draw a `Cube`, `Sphere`, `Plane` or `Quad`, or pick an `.obj`, `.gltf` or
`.glb` file from `Content` with the `...` button. Meshes use the same materials as sprites and
default to `Lit`. For a 3D game turn on `Perspective` on the camera and give it a position and
rotation. The editor opens in 3D. Press Ctrl+3 or the Perspective pill to switch to the flat 2D
view. Sample's `Content/Scenes/Showcase3D.scene` shows every material on meshes, including a
refracting glass sphere.

An `.obj` can hold several materials. Each `usemtl` name becomes a mesh section, and the Material
component shows one slot per section, named after it. A slot left empty uses the component's
`Material`, so one material still covers a whole mesh. Each section draws with its own shader, so
one mesh can mix lit, unlit and glass parts. In a scene file a slot is a line like `Slot 1
"Materials/Glass.material"`. The `Lantern` in `Showcase3D.scene` has bricks, glass, gold and a neon
core in one mesh.

`Transform.Rotation` is euler degrees as pitch (X), yaw (Y) and roll (Z). Cameras and lights point
down their local -Z axis.

## How it works

MeshLibrary loads primitives and imported geometry into GPU meshes. Each material section becomes a
draw using its assigned material. RenderView derives camera axes and view/projection matrices from
the selected camera’s transform and settings, with perspective and orthographic paths.

Source: [MeshLibrary.h](../src/Engine/Render/MeshLibrary.h),
[RenderView.h](../src/Engine/Render/RenderView.h).

## Related guides

[Materials](materials.md), [Skeletal meshes](animation.md), [Transforms](hierarchy.md), [Mouse and
camera input](input.md).
