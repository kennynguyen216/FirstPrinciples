# Spatial audio, acoustics and Doppler

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Spatial audio communicates where sound comes from and how the surrounding scene affects it.
Listener-relative panning and attenuation handle placement, while occlusion, room reflections and
Doppler provide cues about obstacles, spaces and motion.

## Usage and behavior

For positional sound, enable Spatial on a mono source and add an AudioListener to the player or
another entity. The listener follows that entity's position and rotation. Without an explicit
listener, audio uses a primary camera, then the origin. MinDistance and MaxDistance control distance
attenuation in world units. panning follows the listener's right direction. Stereo sources play
without positioning. Master, SFX, Music and UI volumes are available through the editor's Audio...
controls. Pitch changes both pitch and speed.

## Occlusion and acoustic materials

Spatial sources have Occlusion enabled by default. A solid physics collider between source and
listener reduces volume and low-pass filters the sound. Triggers and the source/listener's own
colliders are skipped. Add `PhysicsSystem` when setting up a world yourself. the editor and runtime
already do this. Occlusion checks run at most 20 times per second per voice. Each object crossed
contributes its material's transmission level. two walls multiply their transmission.

Materials expose three acoustic scalars in the inspector, each from 0 to 1:

- Sound Transmission: 0 blocks sound through the object. 1 passes it at full volume.
- Sound Absorption: 0 reflects sound strongly. 1 absorbs reflections and reduces room reverb.
- Sound Damping: 0 preserves high frequencies. 1 heavily muffles transmission and reflections.

For example, `SoundTransmission 0.8`, `SoundAbsorption 0.1`, `SoundDamping 0.15` makes a fairly
transparent, reflective surface. A soft panel might use `0.05`, `0.85`, `0.9`. These are artistic
controls, independent of the material's visual shader. Edits to the shared material affect all
objects using it. Old or missing materials retain 25% transmission and a 1,200 Hz cutoff. Audio uses
the collider object's material (or its nearest parent's), resolving the first material slot for a
mesh. It does not distinguish individual triangles or calculate wall thickness.

## Doppler and rooms



## How it works

AudioWorld estimates source and listener motion from world transforms and performs physics queries
for direct and reflected paths. Materials supply artistic transmission, absorption and damping
values. AudioDevice applies the resulting gains, filtering, pitch and room effect during playback.
These bounded approximations do not solve a full acoustic field.

Source: [AudioWorld.cpp](../src/Engine/Audio/AudioWorld.cpp),
[AudioRoomEffect.h](../src/Engine/Audio/AudioRoomEffect.h), [Material.h](../src/Engine/Material.h).

## Related guides

[Playback and streaming](audio.md), [Demos](audio-demos.md), [Physics](physics.md),
[Materials](materials.md).
