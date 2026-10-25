# Render pipeline and shaders

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The renderer turns scene components into an HDR image and presents it through SDL_GPU. The shared
pipeline keeps editor and runtime materials and effects consistent across Metal and Vulkan.

## Usage and behavior

Rendering uses SDL_GPU. macOS runs on Metal and Windows runs on Vulkan, so Windows machines need a
GPU driver with Vulkan support (any recent NVIDIA, AMD or Intel driver).

The engine ships fixed base shaders. Games pick one per material and set its textures and values.
Game code cannot add shaders yet.

Shader sources live in `src/Engine/Shaders`. The compiled SPIR-V and MSL in `Engine/Content/Shaders`
are committed, so only engine developers who change a shader need the tools:

```bash
brew install glslang spirv-cross
```

```bash
Scripts/CompileShaders.sh
```

On Windows install the [Vulkan SDK](https://vulkan.lunarg.com) and run `Scripts\CompileShaders.ps1`.

## How it works

Renderer collects draws, renders shadow maps and opaque geometry, draws particles and glass, applies
black hole lensing, then composites post-processing and game UI. GPUDevice loads the committed
shader representation for the active backend. Render targets and upload buffers are reused as the
viewport changes.

Source: [Renderer.h](../src/Engine/Render/Renderer.h),
[Renderer.cpp](../src/Engine/Render/Renderer.cpp), [GPUDevice.h](../src/Engine/Render/GPUDevice.h).

## Related guides

[Materials](materials.md), [Batching](batching.md), [Visibility culling](meshlets.md),
[Post-processing](post-processing.md).
