# Meshlet culling and visibility debugging

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Meshlets divide a mesh into small clusters that can be rejected when outside the camera or hidden by
other geometry. The debugger exposes those decisions so visibility errors and workload changes can
be inspected.

## Usage and behavior

Every mesh upload (primitives, OBJ, and glTF) builds clusters using meshoptimizer v0.25. Clusters
have at most 32 vertices and 60 triangles, preserve material sections and triangle winding, and keep
original vertex indices so skeletal weights remain aligned. Meshes are cached by path. uploading a
shared mesh builds its clusters once. The smaller clusters give occlusion tighter bounds, trading
more cluster metadata and indirect commands for finer rejection.

Static opaque meshes use compute frustum and occlusion culling followed by indexed indirect
main-pass draws. A current-frame depth prepass feeds an 8x8 max-depth tile buffer. A cluster is
occluded only when every tile touched by its projected bounds is nearer than the entire cluster.
Near-plane crossings, partially filled edge tiles, and very large projected bounds fail open. This
avoids stale visibility after camera/object movement and supports orthographic cameras. The depth
prepass still submits full occluder geometry. the counters describe the main pass, not total frame
geometry or a guaranteed speedup. Shadows batch compatible static mesh instances.

Open Window > Nanite / Meshlet Debugger for an ImGui window with:

- Enable, frustum, and compute occlusion toggles to compare with full mesh rendering.
- Color meshlets for stable colors per cluster.
- Show outside camera (on when the debugger opens) widens the observer view by 1.35x while
  culling continues from the original camera. The yellow frame marks that camera's frustum, so
  you can see meshlets crossing its edges in real time. Adjust Observer width for more context;
  closing the debugger restores the normal viewport. Picking and gizmos use the wider view too.
- Freeze culling camera to hold visibility at a captured viewpoint while moving the editor camera.
  The yellow outline then shows the frozen camera's volume from your new viewpoint.
- A live viewport preview, plus asynchronous GPU counts of visible, frustum-rejected and occluded
  meshlets and main-pass triangles. Counts are from the last completed readback.

Open `Scenes/Meshlets.scene` in the Sample project and press Play to see the demo camera. Nine
spheres sit behind a movable wall, three sit in front, and two sit outside the camera. In edit mode,
hold right mouse and use WASD in the viewport to move the live camera. The yellow frame and culling
update together. meshlets outside the frame disappear while clusters that cross its edges remain
visible. Disable Frustum culling to compare the surrounding geometry. Freeze the culling camera to
inspect its results from another viewpoint, or move the wall to watch occlusion update. Camera
movement does not require freezing. The feature works in the Runtime too. the debugger is
editor-only. For a reproducible editor smoke test:

```bash
Projects/Sample/Binaries/Debug/Editor --scene Scenes/Meshlets.scene --3d --play --meshlet-debugger --meshlet-colors --screenshot /tmp/meshlets.png --wait 3
Projects/Sample/Binaries/Debug/Tests Meshlet
```

The screenshot command also saves the debugger as `/tmp/meshlets.png.meshlets.png`.

This is a meshlet visibility implementation, with no hierarchical LOD selection, streaming, or
virtualized geometry yet. Animated meshes build meshlets but keep full draws until deformation-aware
bounds are available. Glass and sprites keep their existing paths. Alpha-cutout materials are not
used as occluders, and wireframe disables occlusion. If compute pipelines or buffers cannot be
created, rendering falls back to ordinary indexed draws. The debug window uses the editor's existing
GPU-to-Qt readback approach. close it when measuring rendering performance.

The shader scripts build both Vulkan SPIR-V and Metal MSL, including `.comp` files. Compute sources
use `FP_METAL` for SDL's different Metal buffer binding order. only the MSL compilation defines it.
The committed Vulkan SPIR-V uses SDL's normal descriptor sets. See [SDL compute binding
conventions](https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline).

## How it works

Mesh upload uses meshoptimizer to build clusters. Compute passes test cluster bounds against the
camera and a current-frame depth summary, then produce indexed indirect commands. Conservative
bounds fail open rather than remove uncertain geometry. The ImGui debugger visualizes cluster
colors, camera bounds and completed GPU counters.

Source: [Meshlets.h](../src/Engine/Render/Meshlets.h),
[MeshletCulling.h](../src/Engine/Render/MeshletCulling.h),
[MeshletDebugWidget.cpp](../src/Editor/MeshletDebugWidget.cpp).

## Related guides

[Rendering](rendering.md), [Draw batching](batching.md), [meshoptimizer and ImGui](third-party.md).
