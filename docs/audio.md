# Audio playback and streaming

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

AudioSource components and AudioWorld provide scene audio, overlapping effects and music through a
shared playback system. Resident clips suit short effects, while streaming limits memory use for
long recordings.

## Usage and behavior

Use File > Import Audio... to copy a WAV or MP3 into the open project's `Content/Audio`, or copy it
there yourself. Add an AudioSource component to an entity and use its `Clip` picker to select the
file. Turn on PlayOnStart to hear it when the scene starts, and Loop for ambience. The inspector
provides preview, reload and playback controls. Editor Stop stops game audio and restores the scene.
Imports validate a staged copy and never overwrite an existing destination.

Supported clips are uncompressed PCM16 or float32 WAV at 8-96 kHz, or MP3 (constant or variable
bitrate), mono or stereo. Imports preserve the format. use a matching `.wav` or `.mp3` destination
extension. Files can be up to 4 GiB. Clips over 60 seconds or 32 MiB decoded stream automatically
from disk; short clips share a bounded 128 MiB decoded cache. Each playing file stream uses a
one-second PCM buffer, filled by a background decoder. The audio callback never reads files or
decodes compressed audio. Use the Music bus for music, Loop for repeating ambience, and the usual
pause/stop controls. The inspector identifies streamed files. Keep these files available during
playback. after changing one, stop and choose Reload Clip. Very slow storage can cause audible gaps,
counted in the audio status overruns. Audio output uses SDL3 and MP3 decoding uses the bundled
`dr_mp3` header. no external converter is required. Missing output hardware leaves the editor and
game usable, with an audio status message and a retry control. When output recovers, active loops
and pending PlayOnStart loops restart from the beginning. Explicit pauses, stops and disabled
sources stay in effect. old one-shot effects are not replayed. Preloaded resident clips play from
memory. choose Reload Clip to validate and use file changes.

Game systems can preload a clip in `OnStart` and play overlapping effects during play:

```cpp
world.GetAudio().Preload("Audio/Click.wav");
const FP::AudioPlayResult played = world.GetAudio().PlayOneShot("Audio/Click.wav");
if (!played)
    FP_LOG_WARN("Audio: %s", played.Result.Message.CStr());
```

`Play(entity)` restarts an entity's primary source. `Pause`, `Resume` and `Stop` accept the returned
voice handle. One-shots can optionally follow an owner entity through `FP::OneShotParams`. Scene
replacement, owner destruction and ending play release the affected voices. These APIs run on the
main thread. Preload effects before latency-sensitive triggers: an uncached load is synchronous.

Open `Projects/Sample/Content/Scenes/AudioDemo.scene` and press Play. Move the listener with
WASD/arrows and press Space for overlapping clicks. Demo WAVs are original generated sounds.
regenerate and check them with `python Scripts/GenerateAudioFixtures.py`. Run
`Projects/Sample/Binaries/Debug/Tests Audio` for audio tests.

## Debugging and output control

Open Window > Audio Debugger to inspect the result. The scene and bundled original sounds regenerate
with `python Scripts/GenerateAudioPlayground.py` (FFmpeg and Pillow required only for regeneration).

Run the original acoustics scene directly with:

```powershell
Projects/Sample/Binaries/Debug/Runtime.exe Scenes/Acoustics3D.scene
```

Use 1–4 to compare a soft room, a bright room, a large hall and open air. Space claps, B plays 16
overlapping claps to exercise the limiter, T toggles the automatic clap, R compares dry sound with
room effects, and WASD + mouse walks and looks. The original clap and signs regenerate with `python
Scripts/GenerateAcousticsTest.py` (Pillow required). The demo does not open the microphone.

Each area has a fixed gold sphere on a pedestal labeled CLAP SOURCE. Manual and automatic claps come
from the nearest enabled spatial AudioSource object, so walking around it changes distance, stereo
panning, obstruction and room reflections. Its AudioSource settings control the clip, volume and
range. the debugger names the emitter object rather than the camera.

Press 5 for a moving cyan emitter in the large hall. It plays an original looping 660 Hz tone while
sweeping left and right at up to 30 m/s. P toggles Doppler for comparison. 1–4 stops the tone and
returns to the clap areas. Walking changes the listener's contribution too. The debugger's Pitch x
column shows the applied playback pitch.

Spatial sources have Doppler Scale: 0 disables the shift, 1 uses world units as meters, and values
up to 4 exaggerate it. The radial-velocity calculation uses a 343 m/s sound speed, following the
[OpenAL Doppler model](https://www.openal.org/documentation/OpenAL_Programmers_Guide.pdf). Authored
pitch multiplies the shift. the Doppler ratio is bounded to 0.5–2 and final pitch to 0.25–4. Clip
playback smooths the shift over 50 ms. Motion comes from world positions and game time, including
parent movement. Camera switches, long frame gaps and jumps over 20 m or 300 m/s reset the motion
estimate. Voice chat disables Doppler to keep PCM playback aligned with incoming speech. Reflections
currently share the source's pitch rather than calculating a separate shift for each reflected path.

AudioRoom defines a box interior in meters, with wet level and maximum decay time. Spatial clips and
received voices in the same room get up to six first-order reflections, validated against solid
colliders, plus a synthetic late reverb tail. Put walls at the volume's boundaries. The first
eligible room in creation order wins overlaps. the effect fades at its edges. Each reflection reads
absorption and damping from the material it hits. Objects crossed on either leg also attenuate the
reflection. Area-weighted surface absorption shortens the late tail, and surface damping controls
its tone. This prototype does not model irregular room shapes or sound traveling through doorways.
Run `Tests AudioRoom` and `Tests AudioMaterial` for checks.

Open Window > Audio Debugger (also available from Audio...) during Play for output peak/RMS, limiter
gain reduction, active voices, applied spatial settings, and material hits. Optional ray capture
reuses playback queries: green is direct, orange is obstructed, cyan is reflected, and yellow marks
material hits. Double-click a material hit to inspect its object. Capture keeps at most 64 recent
sources and 64 hits per source. the table shows up to 256 hits. Closing or hiding the panel disables
capture. The debugger does not enable the microphone.

The output limiter links all channels, leaves quiet audio unchanged, and reduces loud overlaps to a
0.98 peak ceiling with a 100 ms release. Normal Stop and replacement fade out over 5 ms; handles
become invalid immediately. Voice-chat mute/disconnect, scene cleanup and device shutdown discard
playback immediately. Stop fades have a separate pool of 65 tails so they do not consume active
voice slots. These are digital sample peaks, not true-peak loudness mastering.

HRTF remains deferred.

## How it works

AudioWorld follows scene ownership and play state, while AudioDevice owns voices and output mixing.
Small clips use a decoded cache and larger files use background decoding into bounded buffers. The
callback mixes ready PCM, applies bus gains and limits output peaks without decoding files on the
audio thread.

Source: [AudioWorld.h](../src/Engine/Audio/AudioWorld.h),
[AudioDevice.h](../src/Engine/Audio/AudioDevice.h),
[AudioFileStream.h](../src/Engine/Audio/AudioFileStream.h),
[AudioLimiter.h](../src/Engine/Audio/AudioLimiter.h).

## Related guides

[Spatial audio and acoustics](spatial-audio.md), [Voice chat](voice-chat.md), [Audio
demos](audio-demos.md).
