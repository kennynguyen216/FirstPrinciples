# Materials and lighting

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Shared material assets describe surface appearance independently of mesh geometry. Several objects
can use one material and update together when its values change.

## Usage and behavior

A sprite draws unlit with its `Tint` and `Texture` until it gets a Material component. That points
at a shared `.material` file in `Content/Materials`. Select the entity and the Details panel shows
the material's values below the component, or a Create Material... button when it has none. Edits
apply to every entity using that material. Save Material or Ctrl+S writes them.

Each material picks a base shader:

| Shader | What it does |
|---|---|
| `Lit` | PBR with base color, normal, ambient occlusion, metallic, roughness, specular, emissive and subsurface maps |
| `Unlit` | Base color times tint plus emissive. No lights needed |
| `Glass` | Transparent. Refracts what is behind it through the normal map, tints it, blurs it with roughness |

Every material has a Cel Shaded checkbox, saved as `CelShaded true` in its `.material` file. Enable
it for stepped lighting and dark silhouette edges on Lit and Glass surfaces. Lit materials keep
their base color texture detail. Glass keeps its refracted background. Unlit materials band their
base color instead. Emission stays smooth so glowing signs and lights keep their brightness. The
toggle defaults to off, including for older material files.

Every map is optional and its scalar multiplies it. `Tiling` and `Offset` move all maps together.
Colors are picked in sRGB. Light comes from `PointLight`, `DirectionalLight` and `AmbientLight`
components (Entity menu has shortcuts). In 2D a point light's `Position.Z` is its height above the
sprites, so raise it to light their faces. Open the Sample project's
`Content/Scenes/Lighting2D.scene` for an example of every material type.

## How it works

MaterialLibrary caches .material assets. Renderer selects Lit, Unlit or Glass pipelines and combines
optional texture maps with scalar settings. Material sections choose their own slots, while lights
and environment data supply illumination to supported shaders. Acoustic values in the same asset are
consumed by audio separately.

Source: [Material.h](../src/Engine/Material.h), [Renderer.cpp](../src/Engine/Render/Renderer.cpp).

## Related guides

[Meshes](meshes-and-cameras.md), [Shadows](shadows.md), [Environment lighting](environment.md),
[Acoustic materials](spatial-audio.md).
