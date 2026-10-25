# Skyboxes and environment lighting

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

An environment panorama supplies both a visible sky and surrounding illumination. This allows metal,
glass and other materials to reflect a coherent scene even without direct lights.

## Usage and behavior

Create a Skybox from Place Entities > Lights or the Entity menu, then choose its Texture in Details.
Use a 2:1 equirectangular panorama under `Content`, up to 8192 pixels wide. Radiance `.hdr` files
retain values above white. PNG, JPG, TGA and BMP panoramas are read as sRGB.

The panorama supplies the visible sky, diffuse light and roughness filtered reflections on lit
materials and glass. Intensity scales the sky and lighting together. Rotation turns both around Y.
Turn off Visible to keep the lighting while showing the camera background, or turn off Enabled to
disable both. The first enabled Skybox in creation order is used. Existing ambient and direct lights
add to the environment lighting.

Open `Scenes/Environment.scene` to compare gold, plastic and glass with no direct or ambient lights.
The original sunset panorama and demo regenerate with `python3 Scripts/GenerateEnvironmentDemo.py`.
Run `Projects/Sample/Binaries/Debug/Tests Environment` for the environment tests.

Panoramas are filtered to at most 512 by 256 for rendering. Diffuse light uses nine spherical
harmonic coefficients and reflections use GGX filtered levels. Changes to the panorama reload it
automatically. Loading and filtering happen on first use and can briefly pause rendering.
Environment lighting does not cast shadows or capture nearby objects. Use direct lights for shadows
and material ambient occlusion maps for local detail. Missing or invalid panoramas use the camera
background and existing lights.

## How it works

EnvironmentMap builds a reduced panorama, nine diffuse spherical harmonic coefficients and filtered
specular levels. Renderer shares those results with lit surfaces, glass and the skybox pass.
Visibility and lighting are independently controllable, and source changes invalidate the cached
environment.

Source: [EnvironmentMap.h](../src/Engine/Render/EnvironmentMap.h),
[Renderer.cpp](../src/Engine/Render/Renderer.cpp).

## Related guides

[Material response](materials.md), [Direct-light shadows](shadows.md), [Reimport](assets.md).
