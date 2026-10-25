# Shadow maps

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Shadow maps show where geometry blocks direct lighting. They help communicate shape and placement
using bounded extra views from supported lights.

## Usage and behavior

The first shadow enabled `DirectionalLight`, `SpotLight`, and `PointLight` of each type cast shadows
from meshes and skeletal meshes onto anything using the `Lit` shader. Glass sections do not cast.
Each type has its own shadow maps, so a directional, spot, and point light can cast together. Point
lights render six depth views to cover all directions. Spot lights render one cone view.

- Each map is `ShadowResolution` pixels square (1024 for directional and spot, 512 for point) in 16 bit depth.
- Directional shadows cover `ShadowDistance` world units just ahead of the camera. Point and spot
  shadows cover their light's `Range`. Meshes outside each region are skipped before drawing.
- Edges are softened by four hardware filtered taps instead of many manual ones.
- The directional box snaps to whole shadow pixels so edges stay still while the camera moves.
- Scenes with no meshes, and the Unlit and Wireframe view modes, skip the pass.

Lower `ShadowResolution` for speed and softer edges. For directional lights, lower `ShadowDistance`
for sharper shadows close up. For point and spot lights, `Range` bounds the shadowed region.
`InnerAngle` and `OuterAngle` control the spot cone. `ShadowStrength` fades shadows for each light.

## How it works

The renderer draws depth from each shadow-casting light before the main scene pass. ShadowMap
constructs the light views and projection bounds, and lit shaders compare surface depth against
those maps with filtered taps. A point light needs six views, so its cost differs from a spot or
directional light.

Source: [ShadowMap.h](../src/Engine/Render/ShadowMap.h),
[Renderer.cpp](../src/Engine/Render/Renderer.cpp).

## Related guides

[Lighting](materials.md), [Instanced shadow draws](batching.md), [Render pipeline](rendering.md).
