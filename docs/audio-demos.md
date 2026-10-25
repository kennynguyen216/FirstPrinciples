# Audio demos and comparisons

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The Sample scenes provide controlled comparisons for streaming, occlusion, reflections, limiting and
Doppler. They make it easier to hear a setting’s effect before applying it to a game.

## Usage and behavior

Open `Scenes/Acoustics3D.scene` in the editor for the original room comparison.

For a longer Doppler demonstration, open `Scenes/DopplerFlyby.scene`. The cyan emitter travels 500
meters at 45 m/s, taking about 11 seconds each way without teleporting at the ends. The listener
starts 80 meters beside the route, with enough audio range to hear the full approach and departure.
P toggles Doppler, F freezes motion while audio continues, and R restarts the pass. WASD, Shift and
the mouse let you walk, run and look along the route. Assign a mono WAV/MP3 to Long Doppler Emitter
- assign mono MP3 to hear your own sound. This scene uses open air with no room effects or occlusion
so the pitch shift is easy to compare. Regenerate its scene and distance markers with `python
Scripts/GenerateDopplerFlyby.py` (Pillow required).

For a ready-to-use MP3 test, open `Scenes/AudioPlayground.scene`. Import your file with File >
Import Audio..., select MP3 Music (M) in the hierarchy, and replace its AudioSource > Clip. Press
Play, click the viewport and press M to start/stop music. Stereo files are supported here; the
included original 65-second MP3 demonstrates automatic streaming. Select MP3 Doppler (5) and assign
a mono MP3 to test your own sound moving through space. Press 5 to start its sweep and P to compare
Doppler on/off. 1–4, Space, T, R and B keep the room, clap, dry/wet and overlap controls below. Open
Window > Audio Debugger to inspect the result. The scene and bundled original sounds regenerate with
`python Scripts/GenerateAudioPlayground.py` (FFmpeg and Pillow required only for regeneration).

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

## How it works

The scene generators create the demo layouts and original audio fixtures. Scripts drive repeatable
motions and control toggles, while the Audio Debugger exposes the same playback settings and ray
queries used by the engine. Follow each generator’s stated prerequisites when regenerating assets.

Source: [GenerateAcousticsTest.py](../Scripts/GenerateAcousticsTest.py),
[GenerateDopplerFlyby.py](../Scripts/GenerateDopplerFlyby.py),
[GenerateAudioPlayground.py](../Scripts/GenerateAudioPlayground.py).

## Related guides

[Audio playback and debugger](audio.md), [Acoustic behavior](spatial-audio.md),
[Testing](testing.md).
