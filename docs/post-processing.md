# Post-processing and accessibility settings

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Scene FXAA and bloom adjust the rendered image, while per-user color assistance improves color
contrast across scenes. Keeping those preferences separate allows accessibility choices to persist
without changing game content.

## Usage and behavior

Open Settings > Engine Settings... for color-blind assistance. Choose None, Protanopia,
Deuteranopia, Tritanopia, Protanomaly, Deuteranomaly, Tritanomaly, Achromatopsia or Achromatomaly,
then adjust Strength from 0 to 1. Changes preview live. Save keeps them in your per-user engine
preferences across scenes, projects and editor/runtime restarts. Cancel restores the previous
values. These preferences are independent of scene files and scene undo/redo.

Use Entity > Post Processing... for scene FXAA and bloom. BloomIntensity controls glow (0-10), and
BloomThreshold selects its minimum HDR brightness before exposure (default 1). WideBloom stacks
eight octaves of blur for a soft halo that spreads across the screen, like film glare around very
bright light. It is off by default, which keeps the tighter original glow. Both effects default to
off. Save the scene to keep them. If more than one entity has PostProcess, the earliest created
entity supplies the settings. Bloom is bypassed in Unlit and Wireframe views.

The color modes assist contrast rather than simulate a deficiency. The first six redistribute lost
color contrast using [Machado's linear RGB
model](https://www.inf.ufrgs.br/~oliveira/pubs_files/CVD_Simulation/CVD_Simulation.html); the
achromatic modes use a heuristic hue-to-brightness mapping. Check with affected players and use
shapes or patterns for critical cues. Game UI and editor gizmos retain their own colors and stay
sharp above post-processing. Run `Tests EngineSettings` and `Tests PostProcess` for persistence,
scene independence, GPU effects, all modes, resizing and scene replacement checks.

## How it works

Scene PostProcess components supply effect settings to Renderer. EngineSettings stores user color
preferences separately. HDR bloom is extracted before exposure, FXAA processes image edges and UI is
composed so its own colors remain readable. Scene snapshots and engine preference persistence follow
separate paths.

Source: [EngineSettings.h](../src/Engine/EngineSettings.h),
[Renderer.cpp](../src/Engine/Render/Renderer.cpp),
[Present.frag](../src/Engine/Shaders/Present.frag).

## Related guides

[Settings menus](editor.md), [Render pipeline](rendering.md), [UI composition](game-ui.md).
