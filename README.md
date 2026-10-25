# FirstPrinciples

A small C++20 game engine with an ECS, a Qt editor and SDL3 rendering. Runs on macOS and Windows.

## Setup

Clone with submodules so the third party libraries come along:

```bash
git clone --recursive https://github.com/kennynguyen216/FirstPrinciples.git
```

In an existing clone run `git submodule update --init`.

### macOS

```bash
Scripts/Setup.sh
Scripts/Build.sh
```

`Setup.sh` installs premake, SDL3, Qt and 7-Zip with Homebrew, then downloads the Ultralight SDK used
for game UI (`Scripts/GetUltralight.sh` does just that part). `Build.sh` builds the Sample project: makefiles
go in `Projects/Sample/Build` and everything builds into `Projects/Sample/Binaries/Debug`. Pass `release`
for an optimised build, and a project folder to build another one, like `Scripts/Build.sh debug Projects/MyGame`.
For Xcode run `Scripts/GenerateXcode.sh` instead.

If an incremental build reports missing symbols after a merge or branch switch, run
`Scripts/Build.sh debug Projects/Sample --clean`. This removes cached output for the selected
configuration before rebuilding. Source files and content are kept.

### Windows

1. Install Visual Studio 2022 or 2026 (the IDE or Build Tools) with the "Desktop development with C++" workload.
2. Install Qt 6 with the [Qt online installer](https://www.qt.io/download-qt-installer) and pick the
   `MSVC 2022 64-bit` kit.
3. MSVC 64-bit kits under `C:\Qt` are detected automatically. For another location, set `QT_DIR`
   to the kit directory and reopen your terminal. MinGW kits cannot link with this MSVC build.
4. Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Setup.ps1
Scripts\GenerateProjects.bat
```

5. Open the generated solution in `Projects\Sample\Build` (`Sample.sln` for VS 2022 or
   `Sample.slnx` for VS 2026), set `Editor` as the startup project and build.
   Pass a project folder to generate another one, like `Scripts\GenerateProjects.bat Projects\MyGame`.

`GenerateProjects.bat` selects the installed VS 2022 or VS 2026 C++ toolset. With Build Tools only,
run `msbuild Projects\Sample\Build\Sample.slnx /m /p:Configuration=Debug /p:Platform=x64` from its Developer
PowerShell or Developer Command Prompt (use `.sln` for VS 2022). Use `Configuration=Release` for an optimised build.
Run `powershell -ExecutionPolicy Bypass -File Scripts\TestGenerateProjects.ps1` to check Windows project generation.

## Packaging a game

Use **File > Package Game > macOS (Release)** or **Windows (Release)** in the editor and choose
an output folder. The target for the other operating system is disabled. Build on macOS for a macOS
app and on Windows for a Windows game. Packaging needs the same compiler, Premake and Qt setup as
building the editor. The first Release build can take a few minutes. Progress and errors appear in
the Output Log, and the editor shows the result when packaging finishes.

The build uses the project's `StartScene`. Save scene, material and script changes before packaging.
The editor offers to save unsaved scenes and scripts. Packaging builds the Release runtime and
`GameScripts`, then puts them together with their required libraries and Ultralight resources.
macOS produces `<Project>.app` and Windows produces a `<Project>-Windows` folder with `Runtime.exe`.
The output must be new so an earlier build is never overwritten. Copy the whole app or folder to
another machine of the same operating system and architecture.

Project `Content` and `Engine/Content` are cooked into `Content.pak`. The cooker stores all files,
including model buffers, textures, audio, scenes, materials, web pages and compiled shaders, with
relative paths and per-file checksums. It preserves their existing runtime formats and does not
compress them or remove unreferenced assets. Symlinks, unsafe paths and names that differ only by
ASCII case are rejected. Game code is linked into the runtime and C++ scripts go into `GameScripts`.
Source code, build tools and editor binaries are left out of the package.

At launch the runtime verifies and unpacks the archive into a private directory under the user's
application data folder. All asset loaders and scripts use that content directory, which is removed
on normal exit. Use `Paths::GetSavedDir()` for writable saved data outside the app. Runtime logs go there too.
Each running game gets its own content directory.
This also lets audio streaming, glTF external files and relative web resources use their normal file
access. A corrupt archive or a missing script library stops a packaged game at startup.

The macOS packager copies the library dependency tree, rewrites library paths and applies local
ad hoc signatures. Public distribution still needs your own signing and notarization. Windows
collects imported DLLs and the installed x64 MSVC redistributable libraries. Qt is used by the
packaging tool and is not included in the game.

After generating projects, packaging is also available from the command line. Build `Runtime` and
`PackageTool` in Release first, then run:

```bash
Projects/Sample/Binaries/Release/PackageTool "$PWD" "$PWD/Projects/Sample" /path/to/Sample.app
```

On Windows use `PackageTool.exe` and a new output folder instead of an `.app` path. Run `Tests Pak`
for archive format, corruption and path validation tests.

## Projects

Each game is a project in its own folder under `Projects`, like Unreal's. A project only sees its own files.

```
Projects/MyGame/
  MyGame.fpproject   name and start scene
  Content/           scenes, materials and textures
  Source/            game code (Game.h, Game.cpp and your components and systems)
  Source/Scripts/    hot reloadable C++ scripts
  Build/ Binaries/ Intermediate/   generated, not committed
  Saved/Logs/        Editor.log and Runtime.log, not committed
```

Engine shaders live in `Engine/Content` and every project shares them.

**File > New Project** in the editor creates a project next to the open one, with an empty scene and
stub game code. **File > Open Project** opens another one. Game code is compiled into the editor, so
opening a project builds that project's editor and switches to it. The first build of a project
compiles the engine too and takes a little while. The Output Log shows its progress.

Each built editor or runtime finds its project by looking for a `.fpproject` file above the executable.

## Making your game

Game code lives in your project's `Source` folder. The editor and the runtime both link it.

1. **Components** are plain structs. Fields can be `bool`, `int32`, `float`, `Vec2`, `Color` or `String`.

   ```cpp
   struct HealthComponent
   {
       int32 Value = 100;
   };
   ```

2. **Register** them in `Game::RegisterComponents` (`Source/Game.cpp`) so the Details panel and scene files see them.

   ```cpp
   registry.Register<HealthComponent>("Health")
       .Field("Value", &HealthComponent::Value);
   ```

3. **Systems** hold the logic. Subclass `FP::System`, loop over a view and add it in `Game::AddSystems`.

   ```cpp
   void Update(FP::World& world, float deltaTime) override
   {
       for (auto [entity, transform, health] : world.View<FP::TransformComponent, HealthComponent>().each())
       {
           if (health.Value <= 0)
               world.DestroyEntity(entity);
       }
   }
   ```

4. **Input** works the same in the editor and the runtime. Ask for actions like
   `FP::Input::WasActionPressed("Jump")` so keys and gamepads both work, or read keys directly with
   `FP::Input::IsKeyDown(SDL_SCANCODE_SPACE)`.

5. **Scenes** live in the project's `Content/Scenes`. Build them in the editor and press Play to run your systems.
   Pause holds the current game state and audio. Step advances one 1/60-second game frame while
   remaining paused. Resume continues play. Stop restores the scene.
   `Runtime` plays `Game::StartScene` or a scene passed on the command line.

Textures are `.png`, `.jpg`, `.tga` or `.bmp` files under `Content/`. Use the `...` button next to a texture
field to pick one.

### Audio

Use **File > Import Audio...** to copy a WAV or MP3 into the open project's `Content/Audio`, or copy it there yourself.
Add an **AudioSource** component to an entity and use its `Clip` picker to select the file.
Turn on **PlayOnStart** to hear it when the scene starts, and **Loop** for ambience. The inspector
provides preview, reload and playback controls. Editor **Stop** stops game audio and restores the scene.
Imports validate a staged copy and never overwrite an existing destination.

Supported clips are uncompressed PCM16 or float32 WAV at 8-96 kHz, or MP3 (constant or variable bitrate),
mono or stereo. Imports preserve the format; use a matching `.wav` or `.mp3` destination extension.
Files can be up to 4 GiB. Clips over 60 seconds or 32 MiB decoded stream automatically from disk;
short clips share a bounded 128 MiB decoded cache. Each playing file stream uses a one-second PCM
buffer, filled by a background decoder. The audio callback never reads files or decodes compressed audio.
Use the **Music** bus for music, **Loop** for repeating ambience, and the usual pause/stop controls.
The inspector identifies streamed files. Keep these files available during playback; after changing one,
stop and choose **Reload Clip**. Very slow storage can cause audible gaps, counted in the audio status overruns.
Audio output uses SDL3 and MP3 decoding uses the bundled `dr_mp3` header; no external converter is required. Missing output hardware
leaves the editor and game usable, with an audio status message and a retry control.

For positional sound, enable **Spatial** on a mono source and add an **AudioListener** to the player
or another entity. The listener follows that entity's position and rotation. Without an explicit
listener, audio uses a primary camera, then the origin. **MinDistance** and **MaxDistance** control
distance attenuation in world units; panning follows the listener's right direction. Stereo sources
play without positioning. Master, SFX, Music and UI volumes are available through the editor's
**Audio...** controls. Pitch changes both pitch and speed.

Game systems can preload a clip in `OnStart` and play overlapping effects during play:

```cpp
world.GetAudio().Preload("Audio/Click.wav");
const FP::AudioPlayResult played = world.GetAudio().PlayOneShot("Audio/Click.wav");
if (!played)
    FP_LOG_WARN("Audio: %s", played.Result.Message.CStr());
```

`Play(entity)` restarts an entity's primary source; `Pause`, `Resume` and `Stop` accept the returned
voice handle. One-shots can optionally follow an owner entity through `FP::OneShotParams`.
Scene replacement, owner destruction and ending play release the affected voices. These APIs run
on the main thread. Preload effects before latency-sensitive triggers: an uncached load is synchronous.

Open `Projects/Sample/Content/Scenes/AudioDemo.scene` and press Play. Move the listener with WASD/arrows and press
Space for overlapping clicks. Demo WAVs are original generated sounds; regenerate and check them
with `python Scripts/GenerateAudioFixtures.py`. Run `Projects/Sample/Binaries/Debug/Tests Audio` for audio tests.

Spatial sources have **Occlusion** enabled by default. A solid physics collider between source
and listener reduces volume and low-pass filters the sound. Triggers and the source/listener's
own colliders are skipped. Add `PhysicsSystem` when setting up a world yourself; the editor and
runtime already do this. Occlusion checks run at most 20 times per second per voice. Each object
crossed contributes its material's transmission level; two walls multiply their transmission.

Materials expose three acoustic scalars in the inspector, each from 0 to 1:

- **Sound Transmission:** 0 blocks sound through the object; 1 passes it at full volume.
- **Sound Absorption:** 0 reflects sound strongly; 1 absorbs reflections and reduces room reverb.
- **Sound Damping:** 0 preserves high frequencies; 1 heavily muffles transmission and reflections.

For example, `SoundTransmission 0.8`, `SoundAbsorption 0.1`, `SoundDamping 0.15` makes a fairly
transparent, reflective surface. A soft panel might use `0.05`, `0.85`, `0.9`. These are artistic
controls, independent of the material's visual shader. Edits to the shared material affect all
objects using it. Old or missing materials retain 25% transmission and a 1,200 Hz cutoff.
Audio uses the collider object's material (or its nearest parent's), resolving the first material
slot for a mesh. It does not distinguish individual triangles or calculate wall thickness.

For proximity voice, add **VoiceChat** and **Network** (`OnePerPeer` enabled) to the player and an
**AudioListener** to its local listening position. Remote player listeners are ignored. Voice
uses the player's world position, distance settings and the same collider occlusion as audio clips.
The microphone starts only after explicit opt-in; hold **V** while the game view has focus to
transmit. Use headsets: speaker echo cancellation is not implemented.

The sample `Scenes/ProximityVoice.scene` has two sounds, a networked player and a wall to walk
around. On Windows, run a host and a joiner from the repository root (use the host's LAN address
instead of `127.0.0.1` when playing on another computer):

```powershell
Projects/Sample/Binaries/Debug/Runtime.exe Scenes/ProximityVoice.scene --host 7777 --voice --password example
Projects/Sample/Binaries/Debug/Runtime.exe Scenes/ProximityVoice.scene --join 127.0.0.1:7777 --voice --password example
```

Without `--voice`, the microphone stays off and a player with VoiceChat can still listen.
The editor's **Audio...** mixer has a separate Voice volume and mute, plus microphone opt-in and
per-peer mute controls when a network system is active. The editor does not create a network
session automatically; the runtime flags above are the first multiplayer test path.

Speech is 48 kHz mono Opus in 20 ms packets over the existing encrypted UDP session. Incoming
speech has a bounded jitter buffer, packet-loss concealment and bounded streaming playback.
Packets are tied to their peer's player, with size and rate limits. Proximity filtering uses
peer-replicated positions; a trusted server/relay is needed to enforce who may receive speech.
The existing session has no relay for peers behind strict NATs.

Open `Scenes/Acoustics3D.scene` in the editor for the original room comparison.

For a longer Doppler demonstration, open **`Scenes/DopplerFlyby.scene`**. The cyan emitter travels
**500 meters at 45 m/s**, taking about **11 seconds each way** without teleporting at the ends.
The listener starts 80 meters beside the route, with enough audio range to hear the full approach
and departure. **P** toggles Doppler, **F** freezes motion while audio continues, and **R** restarts
the pass. **WASD**, **Shift** and the mouse let you walk, run and look along the route.
Assign a mono WAV/MP3 to **Long Doppler Emitter - assign mono MP3** to hear your own sound.
This scene uses open air with no room effects or occlusion so the pitch shift is easy to compare.
Regenerate its scene and distance markers with `python Scripts/GenerateDopplerFlyby.py` (Pillow required).

For a ready-to-use MP3 test, open **`Scenes/AudioPlayground.scene`**. Import your file with
**File > Import Audio...**, select **MP3 Music (M)** in the hierarchy, and replace its **AudioSource > Clip**.
Press Play, click the viewport and press **M** to start/stop music. Stereo files are supported here;
the included original 65-second MP3 demonstrates automatic streaming.
Select **MP3 Doppler (5)** and assign a **mono** MP3 to test your own sound moving through space.
Press **5** to start its sweep and **P** to compare Doppler on/off. **1–4**, **Space**, **T**, **R** and **B**
keep the room, clap, dry/wet and overlap controls below. Open **Window > Audio Debugger** to inspect the result.
The scene and bundled original sounds regenerate with `python Scripts/GenerateAudioPlayground.py`
(FFmpeg and Pillow required only for regeneration).

Run the original acoustics scene directly with:

```powershell
Projects/Sample/Binaries/Debug/Runtime.exe Scenes/Acoustics3D.scene
```

Use **1–4** to compare a soft room, a bright room, a large hall and open air. **Space** claps,
**B** plays 16 overlapping claps to exercise the limiter, **T** toggles the automatic clap,
**R** compares dry sound with room effects, and **WASD + mouse**
walks and looks. The original clap and signs regenerate with `python Scripts/GenerateAcousticsTest.py`
(Pillow required). The demo does not open the microphone.

Each area has a fixed gold sphere on a pedestal labeled **CLAP SOURCE**. Manual and automatic
claps come from the nearest enabled spatial AudioSource object, so walking around it changes
distance, stereo panning, obstruction and room reflections. Its AudioSource settings control
the clip, volume and range; the debugger names the emitter object rather than the camera.

Press **5** for a moving cyan emitter in the large hall. It plays an original looping 660 Hz
tone while sweeping left and right at up to 30 m/s. **P** toggles Doppler for comparison; **1–4**
stops the tone and returns to the clap areas. Walking changes the listener's contribution too.
The debugger's **Pitch x** column shows the applied playback pitch.

Spatial sources have **Doppler Scale**: 0 disables the shift, 1 uses world units as meters, and
values up to 4 exaggerate it. The radial-velocity calculation uses a 343 m/s sound speed,
following the [OpenAL Doppler model](https://www.openal.org/documentation/OpenAL_Programmers_Guide.pdf).
Authored pitch multiplies the shift; the Doppler ratio is bounded to 0.5–2 and final pitch to
0.25–4. Clip playback smooths the shift over 50 ms. Motion comes from world positions and game
time, including parent movement. Camera switches, long frame gaps and jumps over 20 m or
300 m/s reset the motion estimate. Voice chat disables Doppler to keep PCM playback aligned
with incoming speech. Reflections currently share the source's pitch rather than calculating
a separate shift for each reflected path.

**AudioRoom** defines a box interior in meters, with wet level and maximum decay time.
Spatial clips and received voices in the same room get up to six first-order reflections,
validated against solid colliders, plus a synthetic late reverb tail. Put walls at the volume's
boundaries. The first eligible room in creation order wins overlaps; the effect fades at its
edges. Each reflection reads absorption and damping from the material it hits. Objects crossed
on either leg also attenuate the reflection. Area-weighted surface absorption shortens the late
tail, and surface damping controls its tone. This prototype does not model irregular room shapes
or sound traveling through doorways. Run `Tests AudioRoom` and `Tests AudioMaterial` for checks.

Open **Window > Audio Debugger** (also available from **Audio...**) during Play for output peak/RMS,
limiter gain reduction, active voices, applied spatial settings, and material hits. Optional ray
capture reuses playback queries: green is direct, orange is obstructed, cyan is reflected, and
yellow marks material hits. Double-click a material hit to inspect its object. Capture keeps at
most 64 recent sources and 64 hits per source; the table shows up to 256 hits. Closing or hiding
the panel disables capture. The debugger does not enable the microphone.

The output limiter links all channels, leaves quiet audio unchanged, and reduces loud overlaps
to a 0.98 peak ceiling with a 100 ms release. Normal Stop and replacement fade out over 5 ms;
handles become invalid immediately. Voice-chat mute/disconnect, scene cleanup and device shutdown
discard playback immediately. Stop fades have a separate pool of 65 tails so they do not consume
active voice slots. These are digital sample peaks, not true-peak loudness mastering.

HRTF remains deferred.

### C++ scripts

Scripts are per entity behaviour written in C++ in the project's `Source/Scripts`. They build into a separate
`GameScripts` library that the editor reloads while it runs, so you can change code without
restarting, even in the middle of play.

```cpp
#include "Engine/Scripting/Script.h"

using namespace FP;

// Turns the entity every frame. This comment shows in the editor.
FPCLASS(Category = "Movement")
class Rotator : public Script
{
public:
    FPPROPERTY(Range = "0, 720")
    float Speed = 90; // degrees per second, shown as the tooltip

    FPFUNCTION()
    void Reverse() { Speed = -Speed; }

    void OnUpdate(float deltaTime) override
    {
        GetTransform().Rotation.Y += Speed * deltaTime;
    }
};

FP_SCRIPT(Rotator);
```

**Making and attaching scripts**

- In the Details panel, **Add > Script** lists every script, grouped by category, with
  **New Script...** at the bottom. A new script opens in a tab and attaches itself to the entity as
  soon as it compiles. **Scripts > New Script...** and the Content Browser make one without attaching it.
- Drag a script from the Content Browser onto an entity in the viewport to attach it.
- An entity can have any number of scripts. Each shows what it does (the comment above the class),
  which callbacks it runs, its fields and its function buttons.
- Saving a script rebuilds and reloads it (turn this off with **Scripts > Auto Reload**).
  **Scripts > Reload Scripts** (Ctrl+R) forces it. Build output goes to the console, and double
  clicking an error opens the file at that line.

**Reflection**

A header tool, like Unreal's, reads the scripts before every build and generates the reflection
that `FP_SCRIPT` registers. There is nothing to list by hand.

- `FPPROPERTY` exposes a field. It shows in the Details panel, saves with the scene and keeps its
  value across hot reloads. Other members reset when scripts reload. Options: `Tooltip = "..."`,
  `Category = "..."` (fields are grouped under it), `Range = "min, max"` (clamps, and short ranges
  get a slider), `ReadOnly` and `Asset = "Scenes (*.scene)"` (a file picker). A comment on the line
  above or at the end of the line becomes the tooltip.
- `FPFUNCTION` on a function with no arguments adds a button that calls it while playing.
- `FPCLASS` is optional and takes `Category` and `Description`. Without a description, the comment
  right above the class is used.
- Marked members must be public, or put `FP_BODY()` in the class to reflect private ones.
- Mistakes such as a missing declaration, a function with arguments or a private field without
  `FP_BODY()` fail the build with the file and line, like a compiler error.
- Field types: `bool`, `int32`, `float`, `Vec2`, `Vec3`, `Color` and `String`.
- Code the tool does not see can list fields by hand with
  `FP_SCRIPT_FIELDS(Name) { fields.Field("Speed", &Name::Speed).Range(0, 10); }`.

**Helpers**

| | |
|---|---|
| Callbacks | `OnStart`, `OnUpdate(deltaTime)`, `OnStop`, `OnHotReload` (calls `OnStart` by default) |
| This entity | `GetEntity()`, `GetTransform()`, `Get<T>()`, `TryGet<T>()`, `Has<T>()`, `Add<T>()`, `Remove<T>()`, `GetAnimator()`, `GetTime()` |
| Other entities | `FindEntity(name)`, `FindEntities(name)`, `TryGet<T>(entity)`, `IsValid(entity)`, `GetName(entity)` |
| Other scripts | `GetScript<T>(entity)` on an entity (this one by default), `FindScript<T>()` anywhere |
| Spawning | `Spawn(name, position)`, `SpawnScene("Prefabs/Enemy.scene", position)`, `Destroy(entity)`, `DestroySelf()` |
| Scenes | `LoadScene("Scenes/Level2.scene")` switches at the end of the frame. `GetSceneEntity()` |
| Parents | `GetParent()`, `GetChildren()`, `FindChild(name, recursive)`, `SetParent(entity)`, `AddChild(entity)` |
| World space | `GetWorldPosition()`, `SetWorldPosition(position)`, `GetWorldRotation()` |
| Timers | `After(seconds, fn)`, `Every(seconds, fn)`, `CancelTimer(id)`, `CancelAllTimers()` |
| Saving | `SaveData` holds typed values. `Save(slot)`, `Load(slot)`, `Exists(slot)`, `Delete(slot)` |
| Physics | `AddForce(force)`, `AddImpulse(impulse)`, `AddForceAtPoint`, `AddImpulseAtPoint`, `GetVelocity()`, `SetVelocity(velocity)`, `GetMass()`, `GetPhysics()` for the rest |
| Characters | `Move(velocity)` every frame, `Jump(speed)`, `IsGrounded()` |
| Particles | `EmitParticles(count)` bursts from this entity's emitter, or another's |
| Physics queries | `Raycast(origin, direction, distance, hit)`, `SphereCast(origin, radius, direction, distance, hit)`, `RaycastFromCamera(distance, hit)`, `OverlapSphere(center, radius)`, `AddExplosion(center, radius, impulse)` |
| Cameras | `SetActiveCamera(entity or name)` (this entity by default), `GetActiveCamera()`, `IsActiveCamera()` |
| Physics events | `OnCollisionEnter(other, collision)`, `OnCollisionExit(other)`, `OnTriggerEnter(other)`, `OnTriggerExit(other)` |
| Math | `Clamp`, `Lerp`, `InverseLerp`, `MoveTowards`, `Damp`, `Random::Range`, `Random::Chance`, `Random::Direction` |
| Input actions | `Input::IsActionDown("Fire")`, `Input::WasActionPressed("Jump")`, `Input::GetActionValue("Fire")`, `Input::GetActionAxis("Move")` |
| Gamepads | `Input::IsGamepadButtonDown(GamepadButton::South)`, `Input::GetGamepadAxis(GamepadAxis::RightTrigger)`, `Input::GetGamepadStick(right)`, `Input::Rumble(low, high, seconds)` |
| Input and logs | `Input::IsKeyDown(SDL_SCANCODE_SPACE)`, `Input::GetMoveAxis()`, `Input::GetMouseDelta()`, `Input::SetCursorMode(mode)`, `FP_LOG("...")` |

**Mouse and cameras**

While the game plays the mouse is hidden and held in place, so `Input::GetMouseDelta()` drives
mouse look. Call `Input::SetCursorMode(CursorMode::Visible)` in `OnStart` for menus and HUDs with
buttons, or `Hidden` to draw your own pointer. In the editor Esc frees the mouse and a click on
the viewport takes it back.

The game sees through the first camera with `Primary` on. `SetActiveCamera` cuts to another one
by turning `Primary` off everywhere else.

**Input actions and gamepads**

Choose **Edit > Input Actions...** or double click `Input.actions` in the Content Browser to edit
bindings in a tab. Each row has an action name, device, input, optional direction and scale.
Use **Add Binding** and **Remove Selected** to change the rows. The input picker offers the names
accepted by each device. Leave a scale blank to keep its default behavior, especially for sticks.

The **Source** view edits the same document and preserves comments. Undo and redo work in both
views. Save with Ctrl+S, the tab's **Save** button or **Save All**. Invalid lines are listed below
the bindings and cannot be saved. Click an error to jump to its source. Unsaved edits are checked
when closing a tab, quitting or packaging. Changes apply the next time Play starts and when the
runtime next opens the project. Opening the editor from the Edit menu recreates a missing
`Input.actions` with the defaults.

Gameplay scripts can also change bindings immediately through `Input`:

```cpp
const auto names = Input::GetActionNames();
const auto bindings = Input::GetActionBindings("Jump");
Input::RebindAction("Jump", 0, "Key:G");
Input::SetActionBindings("Jump", { "Key:G", "Button:South" });
Input::SetActionBindings("Jump", {}); // leave the action unbound
Input::ResetActionBindings("Jump");
Input::ResetAllActionBindings();
```

Replacement strings use the same device, direction and scale syntax as `Input.actions`.
Invalid replacements leave the current bindings alone. Reset restores the bindings from the most
recent `LoadActions` or `LoadActionsFile`, so project defaults stay separate from player preferences.

For a menu that asks the player to press a new control, call
`Input::BeginActionRebind("Jump", 0)`, then check `Input::GetActionRebindStatus()` for `Listening`,
`Complete` or `Cancelled`. `GetLastReboundInput()` returns the completed binding string.
Capture accepts fresh keyboard, mouse button and gamepad button presses. Escape or
`CancelActionRebind()` cancels, and losing focus cancels too. Held controls are ignored.
Named actions are suppressed during capture and until the captured control is released.

Pass `true` as the third argument to capture individual gamepad axes too. An axis must first
return near its center and then move past 75 percent. Capturing preserves the binding's direction
and scale, with negative axis movement inverted to match the chosen direction. Mouse motion is
ignored during capture. Use `SetActionBindings` for whole sticks or mouse delta bindings.

Use `Input::SaveActionBindings()` to save player overrides and `Input::LoadActionBindings()` at
play start to restore them. Both accept an optional SaveData slot name for profiles. These files
live in the project's user save folder and leave `Content/Input.actions` unchanged. Only changed
actions are saved, so new project actions remain available. Failed loads leave current bindings
alone. Saving after a reset keeps the reset for the next launch.

Open `Scenes/InputBindings.scene` and press Play for a working menu, backed by the
`InputRebindMenu` script and `UI/InputBindings.html`. It lists bindings, captures replacements,
accepts typed bindings and saves or restores controls through the existing `engine.send` bridge.
The Jump counter lets you try the new binding after releasing its capture press. The menu uses
the same optional Ultralight SDK as other game UI pages.

Games ask for actions like Jump or Move instead of keys, so the keyboard, mouse and any gamepad
all play. `Content/Input.actions` binds them, one input per line, and new projects get a copy of
the defaults:

```
Jump   Key:Space
Jump   Button:South          # A on Xbox, Cross on PlayStation
Move   Key:W        +Y       # keys point a 2D action, right and up positive
Move   Stick:Left
Look   Mouse:Delta
Look   Stick:Right  800      # a scale makes a stick turn at a steady speed
Fire   Axis:RightTrigger
```

Devices are `Key` (SDL key names with `_` for spaces), `Mouse` (`Left`, `Right`, `Middle`, `Delta`),
`Button` and `Axis` for gamepads (named by position, like `South`, `LeftShoulder`, `DpadUp` or
`RightTrigger`) and `Stick` (`Left` or `Right`). The defaults bind Move, Look, Jump, Run, Fire, Aim,
Interact and Pause. The editor reloads the file each time Play starts.

```cpp
if (Input::WasActionPressed("Jump"))
    Jump(6);
const Vec2 move = Input::GetActionAxis("Move"); // keys and the left stick
if (Input::IsActionDown("Fire"))
    Input::Rumble(0.3f, 0.6f, 0.1f);
```

Every connected pad counts, so whichever one is picked up plays. Sticks have a round dead zone so
they read zero at rest.

`SpawnScene` copies every entity in a scene file with new ids, so any scene works as a prefab and
can be spawned many times. Timers stop when the script stops or reloads.

**Saving and loading**

`SaveData` saves progress and settings as named values. It is available from `Script.h`, or
include `Engine/SaveData.h` in other game code. Use a separate slot for each profile or purpose:

```cpp
SaveData progress;
progress.SetInt("score", Score);
progress.SetVec3("position", GetWorldPosition());
if (!progress.Save("player-1"))
    FP_LOG_WARN("Could not save progress");

SaveData loaded;
if (loaded.Load("player-1"))
{
    Score = loaded.GetInt("score", 0);
    SetWorldPosition(loaded.GetVec3("position", GetWorldPosition()));
}

SaveData settings;
settings.Load("settings");
float volume = settings.GetFloat("volume", 1.0f);
settings.SetFloat("volume", volume);
if (!settings.Save("settings"))
    FP_LOG_WARN("Could not save settings");
```

Use `SetBool`, `SetInt`, `SetFloat`, `SetVec2`, `SetVec3`, `SetColor` and `SetString`, with matching
`Get` functions. Getters return the fallback when a key is missing or has another type. This lets
new settings have defaults when loading older saves. `Has(key)`, `Remove(key)`, `Clear()` and `Num()`
inspect and edit the values. `SaveData::Exists(slot)` checks for a file and `SaveData::Delete(slot)`
removes one. An existing file can still fail to load if it is damaged.

Slot names use 1 to 64 lowercase letters, digits, underscores or hyphens. Files are readable text
with a format version and keep their value types. Each project name gets its own folder under
the current user's application data, so saves work when the game is installed in a read only
folder. `Paths::GetSaveDir()` gives the folder. On macOS it is
`~/Library/Application Support/FirstPrinciples/<ProjectName>/Saves`. On Windows it is under
`%APPDATA%/FirstPrinciples/<ProjectName>/Saves`. Editor play and the runtime share these saves,
including across script reloads. Projects with the same name share saves.

Save and load calls are synchronous and return `false` on failure. A failed load leaves the
current values alone. Saving writes and flushes a temporary file before replacing the previous
save. Invalid slot names, nonfinite numbers, unsupported format versions and files over 16 MiB
are rejected. `SaveToString()` and `LoadFromString(text)` use the same format in memory, and
`SaveToFile(path)` and `LoadFromFile(path)` accept explicit paths for tools. Save at checkpoints
or when settings change, since disk access can stall a frame. SaveData does not capture the
world or script fields automatically.

The Sample project's `Checkpoint` script saves a player's position and score. Attach it to an
entity and use **Save Progress** and **Load Progress** in Details while playing. It loads its
slot at play start. Run `Projects/Sample/Binaries/Debug/Tests Save_` for the save tests.

**Scene scripts**

Click the scene's row at the top of the Hierarchy to edit the scene itself. Scripts added there
run for the whole scene, which suits game rules, waves of enemies or score. They live on a hidden
**Scene** entity that saves with the scene, so `FindScript<GameRules>()` reaches them from anywhere.

Components still live in `Source` and are registered in `Game::RegisterComponents`. Keep new
component types out of `Source/Scripts`, since data owned by the scripts library would vanish
when it reloads.

The runtime loads the same library at startup. Building from the command line works as before,
and the `GameScripts` project builds along with the editor, running the header tool first.

### Physics

Physics runs on [Jolt](https://github.com/jrouwe/JoltPhysics) while the game plays.

- A **Collider** gives an entity a shape: `Box` (`Size`), `Sphere` (`Radius`), `Capsule` (`Radius` and
  `Height`, standing along Y) or `Mesh` (the entity's own mesh). Sizes scale with the transform and
  `Offset` moves the shape. On its own a collider is a solid, unmoving part of the level.
- A **RigidBody** makes it move. `Dynamic` bodies fall, bounce and get pushed. `Kinematic` bodies
  follow their transform, so scripts or animation move them, and they shove dynamic bodies aside.
  `Static` never moves. `Mass` is in kilograms, `Restitution` is bounciness, `LockRotation` keeps
  characters upright and `Continuous` stops fast, small bodies tunnelling through thin walls.
- A collider with `Trigger` on reports overlaps instead of blocking.
- Moving a dynamic body's transform by hand teleports it. Changing its collider or rigid body
  rebuilds it. Dynamic mesh colliders use the mesh's convex hull, static ones use every triangle.
- Physics steps at a fixed 60 times a second whatever the frame rate. Children of other entities
  work too, and their bodies sit at their world position.
- The editor outlines colliders in green, and triggers in amber.

Scripts push bodies and hear about contacts:

```cpp
void OnUpdate(float) override
{
    if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
        AddImpulse({ 0, 5, 0 });
    RaycastHit hit;
    if (Raycast(GetWorldPosition(), { 0, -1, 0 }, 1.1f, hit))
        FP_LOG("Standing on %s", GetName(hit.Entity).CStr());
}

void OnCollisionEnter(Entity other, const Collision& collision) override { /* collision.Point, collision.Normal */ }
void OnTriggerEnter(Entity other) override { ++Score; }
```

`GetPhysics()` also has `AddTorque`, angular velocity and gravity, and takes any entity, like
`GetPhysics().AddImpulseAtPoint(hit.Entity, push, hit.Point)` to shove what a ray hit.
`Content/Scenes/Physics.scene` drops balls down a ramp, through a goal that counts them, and into a
pyramid of crates. Space kicks every ball. C switches to a first person camera where the mouse
looks, WASD flies, left click pushes, F picks things up and right click blows them up.

**Characters**

A **CharacterController** makes a walking capsule for players and enemies, used instead of a
collider and rigid body. It slides along walls, climbs steps up to `StepHeight`, walks up slopes
up to `MaxSlope`, rides moving platforms and pushes crates. Rays and triggers find it like any
body.

**Entity > Create Character** (or Character in Place Entities) adds a pill with a follow camera
that plays with no code in any project. Its **PlayerInput** component plays it from the Move, Look,
Run and Jump actions: WASD or the left stick walks, Shift runs, Space or South jumps and the mouse
or right stick turns it. Remove PlayerInput to drive it from a script:

```cpp
void OnUpdate(float) override
{
    const Vec2 axis = Input::GetActionAxis("Move");
    Move(Vec3(axis.X, 0, -axis.Y) * 5); // meters per second, call it every frame
    if (Input::WasActionPressed("Jump"))
        Jump(6);                        // only works on the ground
}
```

`AirControl` sets how much `Move` steers mid jump. `SetVelocity` and `AddImpulse` knock a
character about, and explosions push it too. `Content/Scenes/Character.scene` has a third person
player with stairs, a walkable and a too steep ramp, a lift, crates and a zone
that counts the player walking in.

### Black holes

A **BlackHole** bends light like the real thing. Every pixel's light ray is traced backwards through
Schwarzschild spacetime, so you get the black shadow at 2.6 times the horizon radius, the photon ring,
the far side of the accretion disk lensed up over the top and a thinner image wrapped underneath.
A skybox is bent all the way around. Without one, procedural stars fill the sky.

- `SchwarzschildRadius` is the event horizon in world units. `SpeedOfLight` is a game-scale c in world
  units per second. Lower it and relativity shows up at slower speeds, and the disk spins faster.
- The disk lies in the transform's local XY plane, so rotate the entity to tilt it. It starts at the
  innermost stable orbit (3 radii) and runs to `DiskOuterRadius`. Each ring glows as a blackbody at
  its own temperature, shifted by Doppler and gravitational redshift, so the side spinning toward
  you is brighter and bluer. `DiskTemperature` sets the hottest ring (4500 K glows orange, 9000 K
  white-blue).
- Up to four holes render at once. Moving a hole is fine.

A **GravityBody** is pulled by every black hole with the exact Schwarzschild orbit equation, so orbits
precess, nothing stays in orbit inside 3 radii and anything under 1.5 radii spirals in. `Captured`
turns on when it crosses a horizon, for your game to respawn it or end the race. With a Dynamic
**RigidBody** the pull goes through physics (set `GravityScale` to 0 in space). Without one,
`Velocity` moves the transform. `Thrust` lets the Move action push it.

A still black hole bends a racer's path but hands back the same speed it came in with. Speed comes
from thrusting at the closest pass, where you are fastest, or from swinging behind a moving hole.
`Content/Scenes/BlackHole.scene` has a racer to fly around one with WASD.

### Particles

A **ParticleEmitter** sprays small camera facing quads for fire, smoke, sparks, dust and magic.
**Entity > Create Particle Emitter** (or Particle Emitter in Place Entities) adds one, and emitters
run in the editor too, so effects can be tuned without playing.

- `Rate` streams particles every second and `Burst` fires a batch when play starts.
  `MaxParticles` caps how many are alive at once.
- Each particle lives for `Lifetime` seconds, leaving from a `Point`, `Sphere` or `Box` along
  `Direction`, within `Spread` degrees of it, at `Speed`. The random settings vary each one.
- `Gravity` pulls them down (1 is normal, negative rises) and `Drag` slows them.
- Size and color fade from `StartSize` and `StartColor` to `EndSize` and `EndColor`. An alpha of
  zero at the end fades them out.
- With no `Texture` each particle is a soft dot. `Additive` blending glows, `Alpha` covers, which
  suits smoke.
- `WorldSpace` leaves particles behind as the emitter moves. Turn it off for trails that move
  with it.
- `DestroyWhenDone` removes the entity once it stops emitting and its particles are gone, for one
  off effects:

```cpp
const Entity blast = Spawn("Sparks", hit.Point);
ParticleEmitterComponent& sparks = GetWorld().Add<ParticleEmitterComponent>(blast);
sparks.Rate = 0;
sparks.Burst = 80;
sparks.Spread = 180;
sparks.DestroyWhenDone = true;
```

`Content/Scenes/Particles.scene` has a campfire with smoke, a fountain and a ball of sparkles.

### Game UI

Game UI is written in HTML, CSS and JavaScript and drawn by [Ultralight](https://ultralig.ht).
`Scripts/Setup.sh` fetches its SDK (or run `Scripts/GetUltralight.sh`, `.ps1` on Windows). Without it
everything still builds and UI simply does not draw.

Add a **UI** component and pick a page from `Content`, or drag an `.html` file onto an entity in the
viewport (onto empty space puts it on the primary camera). Like a Unity canvas, a UI on a camera
draws over that camera's view, and a UI on any other entity draws over every view. `Order` stacks
several pages and `Input` lets a page take the mouse and keyboard. Pages show while the game plays.

**In the page**, a global `engine` object talks to the game:

```html
Score: <b data-bind="score">0</b> <span id="result"></span>
<button onclick="engine.send('jump', { height: 2 })">Jump</button>
<script>
    engine.on("gameOver", function (data) { document.getElementById("result").textContent = data.won ? "You won" : "You lost"; });
    engine.watch("health", function (value) { document.body.classList.toggle("low", value < 20); });
</script>
```

- `data-bind="key"` elements show `engine.state[key]` and update by themselves.
- `engine.watch(key, fn)` runs whenever a script sets the key. `engine.on(name, fn)` hears events.
- `engine.send(name, data)` reaches scripts. Any JSON value works as data.
- `console.log` and page errors go to the Output Log. Saving the page, its CSS or its JS while
  playing reloads it, keeping the values scripts set.

**In a C++ script** on the same entity, or a scene script:

```cpp
void OnUpdate(float) override
{
    GetUI()->Set("score", Score);                 // numbers, bools and text
    if (Lives == 0)
        GetUI()->Emit("gameOver", "{\"won\":false}");
}

void OnUIEvent(std::string_view name, const Json& data) override
{
    if (name == "jump")
        Jump(data["height"].AsNumber(1));
}
```

`GetUI(entity)` reaches another entity's page. `RunScript(js)` runs JavaScript in the page and
`LoadPage(path)` swaps pages. `Json` reads what pages send: `data["player"]["name"].AsString()`.
**Add > New UI Page...** in the Content Browser writes a starter page, and double clicking pages,
CSS and JS opens them in a code tab. The 3D showcase camera has a HUD (`Content/UI/HUD.html` and
`Source/Scripts/ShowcaseHUD.cpp`) with live state, a Jump button, a speed slider and a shadow switch.

Ultralight's free license covers commercial use for teams under US$200K in revenue and in funding.
The free edition caps pages at 60 frames per second, runs JavaScript without a JIT and has no video.
Pages render on the CPU and only changed frames upload to the GPU.

### Parents and children

Drag an entity onto another in the Hierarchy to make it a child, or onto the scene row to make it
top level again. Right click for **Create Child** and **Detach from Parent**. A child's Transform
is relative to its parent, so it moves, turns and scales along with it. The gizmo still works in
world space. Deleting, copying and duplicating an entity takes its children with it.

### Materials and lighting

A sprite draws unlit with its `Tint` and `Texture` until it gets a **Material** component. That points
at a shared `.material` file in `Content/Materials`. Select the entity and the Details panel shows the
material's values below the component, or a **Create Material...** button when it has none. Edits apply
to every entity using that material. **Save Material** or Ctrl+S writes them.

Each material picks a base shader:

| Shader | What it does |
|---|---|
| `Lit` | PBR with base color, normal, ambient occlusion, metallic, roughness, specular, emissive and subsurface maps |
| `Unlit` | Base color times tint plus emissive. No lights needed |
| `Glass` | Transparent. Refracts what is behind it through the normal map, tints it, blurs it with roughness |

Every map is optional and its scalar multiplies it. `Tiling` and `Offset` move all maps together.
Colors are picked in sRGB. Light comes from `PointLight`, `DirectionalLight` and `AmbientLight`
components (Entity menu has shortcuts). In 2D a point light's `Position.Z` is its height above the
sprites, so raise it to light their faces. Open the Sample project's `Content/Scenes/Lighting2D.scene` for an example of
every material type.

### 3D

Add a **Mesh** component to draw a `Cube`, `Sphere`, `Plane` or `Quad`, or pick an `.obj`, `.gltf` or
`.glb` file from `Content` with the `...` button. Meshes use the same materials as sprites and default to `Lit`.
For a 3D game turn on `Perspective` on the camera and give it a position and rotation.
The editor opens in 3D. Press Ctrl+3 or the Perspective pill to switch to the flat 2D view. Sample's `Content/Scenes/Showcase3D.scene`
shows every material on meshes, including a refracting glass sphere.

An `.obj` can hold several materials. Each `usemtl` name becomes a mesh section, and the Material
component shows one slot per section, named after it. A slot left empty uses the component's
`Material`, so one material still covers a whole mesh. Each section draws with its own shader, so one
mesh can mix lit, unlit and glass parts. In a scene file a slot is a line like `Slot 1 "Materials/Glass.material"`.
The `Lantern` in `Showcase3D.scene` has bricks, glass, gold and a neon core in one mesh.

`Transform.Rotation` is euler degrees as pitch (X), yaw (Y) and roll (Z). Cameras and lights point
down their local -Z axis.

### Shadows

The first shadow enabled `DirectionalLight`, `SpotLight`, and `PointLight` of each type cast shadows
from meshes and skeletal meshes onto anything using the `Lit` shader. Glass sections do not
cast. Each type has its own shadow maps, so a directional, spot, and point light can cast together.
Point lights render six depth views to cover all directions. Spot lights render one cone view.

- Each map is `ShadowResolution` pixels square (1024 for directional and spot, 512 for point) in 16 bit depth.
- Directional shadows cover `ShadowDistance` world units just ahead of the camera. Point and spot
  shadows cover their light's `Range`. Meshes outside each region are skipped before drawing.
- Edges are softened by four hardware filtered taps instead of many manual ones.
- The directional box snaps to whole shadow pixels so edges stay still while the camera moves.
- Scenes with no meshes, and the Unlit and Wireframe view modes, skip the pass.

Lower `ShadowResolution` for speed and softer edges. For directional lights, lower `ShadowDistance`
for sharper shadows close up. For point and spot lights, `Range` bounds the shadowed region.
`InnerAngle` and `OuterAngle` control the spot cone. `ShadowStrength` fades shadows for each light.

### Skeletal meshes and animation

A **SkeletalMesh** component draws a `.gltf` or `.glb` with a skin. Its bones bend the mesh on the GPU,
up to 128 bones and four per vertex. glTF primitives become mesh sections named after their glTF
materials, so the Material component's slots work the same as for `.obj` files. Dropping a skinned
glTF into the viewport makes a skeletal mesh with an **Animator** that loops its first clip.

The Animator loops its `Clip` while the game plays, at `Speed`. The Details panel lists the clips the
mesh has. For anything more, build a state machine from a C++ script:

```cpp
void OnStart() override
{
	Animator* animator = GetAnimator();
	animator->AddState("Idle", "Idle");
	animator->AddState("Walk", "Walk");
	animator->AddState("Jump", "Jump", false); // plays once and holds its last frame
	animator->AddTransition("Idle", "Walk", 0.2f).When("Speed", AnimCompare::Greater, 0.1f);
	animator->AddTransition("Walk", "Idle", 0.25f).When("Speed", AnimCompare::Less, 0.1f);
	animator->AddTransition("", "Jump", 0.1f).When("Jump", AnimCompare::Triggered); // from any state
	animator->AddTransition("Jump", "Idle").AfterExitTime(1);
}

void OnUpdate(float) override
{
	GetAnimator()->SetFloat("Speed", speed);
	if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
		GetAnimator()->SetTrigger("Jump");
}
```

- The first state added plays first. Each transition blends over its time in seconds.
- Parameters are floats. `SetBool` stores 1 or 0. A trigger stays set until a transition uses it.
- All of a transition's conditions must pass. `AfterExitTime(fraction)` also waits for that much of
  the clip, so `1` leaves at the end.
- `Play(state)` jumps straight to a state. `IsInState`, `GetNormalizedTime` and `GetClipLength` read back.
- Adding a state or transition that already exists replaces it, so `OnStart` can run again after a
  hot reload. Animators live while the game plays and reset when it stops. The editor shows the
  bind pose.

The `Noodle` in `Showcase3D.scene` uses `Source/Scripts/NoodleController.cpp`: WASD or the arrows walk it
and Space jumps. `Scripts/GenerateSampleCharacter.py` writes its `Characters/Noodle.gltf`.

### Editor layout

The editor follows Unreal Engine 5's layout and dark theme, using this engine's own terms.

- **Place Entities** on the left: click an entry to create it, or drag it into the viewport to place it
  under the mouse. The icons on top filter by category.
- **Viewport** in the middle, in a tab named after the open scene. The pills over it switch between
  Perspective and Top (2D), pick Lit, Unlit or Wireframe, and turn grid snap on with a grid size.
- **Details** and **Hierarchy** share the panel on the right as tabs. Details shows the selection's components in collapsible sections. Add components with the Add
  button, remove one with the x on its header, and filter fields with the search box.
- **Hierarchy** lists every entity nested under its parent with its type, plus a search box. The scene row at the top holds scene scripts.
- Drag a panel by its title to move it, or drop it on another panel to share a spot as tabs. Pull a tab
  out to separate it again. **Window** reopens closed panels and **Reset Layout** puts everything back.
- **Content Browser**, **Output Log** and **Profiler** share the bottom. The profiler shows each
  system's last and smoothed average CPU update time, plus the total game update time. Rendering and
  editor UI time are not included. The status bar opens each panel and has a console box: type
  `help` for commands including play, pause, resume, step and stop.

The Hierarchy tab is active when the editor opens. Selecting an entity or asset brings Details
forward so its properties can be edited.

### Content browser

The **Content Browser** tab next to the **Output Log** at the bottom of the editor shows everything in
the project's `Content` plus its scripts in `Source/Scripts`. Textures show image thumbnails with a
checkerboard behind transparency. Materials show a lit sphere, and OBJ, glTF and GLB meshes show
a fitted preview in their bind pose. Previews fill in as you browse, with type icons for unreadable assets.
Changes to assets or their texture and buffer files refresh the thumbnails automatically.

- Double click a scene to open it or a script to edit it in a tab. Click a material to edit it in
  the Details panel, or a texture to preview it.
- Drag a texture into the viewport to make a sprite, an `.obj` to make a mesh, or a material onto a
  sprite or mesh to assign it. Drag any asset onto a matching Details field to fill it in.
- **Add** or a right click makes a New Folder, Material, Scene or Script. **Import** copies files in,
  and **Save All** saves the scene, materials and script tabs. Back, forward and the folder path
  above the files move between folders.
- Right click an asset to Rename (F2), Duplicate, Delete, Copy Path or Show in Finder or Explorer.
- Renaming keeps asset IDs and migrates old scene and material paths to IDs. Delete moves files and their metadata to the system Trash.
- The browser refreshes on its own when files change, and the Output Log comes forward when a script
  build fails.

### Asset pipeline

Every file under a project's `Content` has a versioned `.fpmeta` file beside it. The editor creates
missing metadata automatically. **Commit these files with the assets**. They contain the asset's
stable ID and import settings. Moving an asset outside the editor requires moving its metadata too.
Deleting metadata and importing again gives the asset a new ID.

Scene and material asset fields save references as `asset://<guid>`. Inspectors show the resolved
path, and loaders resolve the ID to the current file. Existing path references still load and
convert to IDs when saved. Renaming in the Content Browser keeps the ID, updates legacy references
on disk, and keeps unsaved scene and material edits usable. Duplicates get new IDs and retain their
import settings. The project start scene is retained by ID when assets are renamed.

Select a texture to edit **Generate mipmaps** and **Color space**, then choose **Apply and Reimport**.
**Auto** uses sRGB for color maps and linear for data maps. **sRGB** and **Linear** override that choice
for every use of the texture. The same settings are used in the editor and packaged games.
Original files remain the import sources, so there are no machine-specific source paths to repair.
Game code can call `Paths::AssetReference(path)` when a stored reference must survive a rename.

The registry tracks scene and material asset fields, glTF and GLB external buffers and images,
OBJ material libraries, MTL texture maps, exposed script asset fields, and static HTML, CSS and
JavaScript resource links. Content changes refresh the registry and invalidate affected texture, mesh, skeletal model and
material caches on their next use. UI pages reload when their tracked resources change. Material
reimport preserves unsaved edits. Audio changes apply to subsequent loads and playback while existing voices retain their current audio snapshot.

Use **Validate** in the Content Browser to report missing dependencies, duplicate IDs, invalid
metadata, unsafe paths and names that differ only by case. The Output Log includes each issue's
asset path. Packaging runs the same validation before staging a game. Cooked content includes the
metadata, and the runtime reads it without writing into Content.

After building, the same pipeline is available without opening the editor:

```bash
Projects/Sample/Binaries/Debug/AssetTool --import
Projects/Sample/Binaries/Debug/AssetTool --validate
Projects/Sample/Binaries/Debug/AssetTool --dependencies Scenes/Showcase3D.scene
Projects/Sample/Binaries/Debug/AssetTool --project Projects/MyGame --import
Projects/Sample/Binaries/Debug/Tests Assets
python3 Scripts/TestAssetPipeline.py
```

Use `AssetTool.exe` on Windows. `--import` creates missing metadata and preserves existing IDs and
settings. Validation returns a nonzero exit code when issues are found. A read-only scan accepts
legacy projects without metadata, but ID references require their matching metadata files.

Dependency discovery covers static references. Script path discovery needs the corresponding built
script types and fields marked with `Asset`. Explicit ID references are tracked even for unknown
scripts. Paths constructed in game code or JavaScript need to be managed by the game. glTF, OBJ and web resources retain their format's relative references,
so moves that would break those links are rejected. Move a self-contained resource folder together,
or update its source references first. Imports and refreshes are synchronous, and the cooker still
preserves source formats without compression or unused-asset removal.

### Undo and redo

Ctrl+Z undoes and Ctrl+Shift+Z redoes (Cmd on macOS, and Ctrl+Y also redoes on Windows). Both are in the
**Edit** menu too.

- Every scene edit can be undone: moving, creating, duplicating, deleting, dropping assets, Details
  fields, components, scripts and material values. The selection comes back with each step.
- A drag or a burst of typing becomes one step once the mouse comes up and edits pause briefly.
- Script tabs and text fields keep their own text undo while they have focus.
- History holds the last 100 steps and starts over when a scene is opened. Play mode changes are not
  recorded since Stop puts the scene back anyway.

### Logging

Log with printf style formatting from anywhere, scripts included:

```cpp
FP_LOG("Loaded %d enemies", count);
FP_LOG_VERBOSE("Path took %.2f ms", ms);
FP_LOG_WARN("No spawn point in %s", scene);
FP_LOG_ERROR("Could not open %s", path);
```

Logging runs on [spdlog](https://github.com/gabime/spdlog), and every line goes to four places:

- The terminal, with the level in color. Warnings and errors go to stderr.
- The project's `Saved/Logs/Editor.log` or `Runtime.log`, with the date, time and thread. Each run
  starts a fresh file and keeps the last three runs as `.1.log`, `.2.log` and `.3.log`.
- The editor's **Output Log**, with the time.
- The Visual Studio Output window on Windows.

`Log::SetMinimumLevel` hides lines below a level, `Log::AddSink` adds another output and `Log::Flush`
writes out anything buffered. Only verbose lines are ever held back, so a crash never loses a warning.

### Multiplayer

Games can be peer to peer over UDP. Every peer talks to every other peer directly, with no server.

```bash
Projects/Sample/Binaries/Debug/Runtime --host 7777
Projects/Sample/Binaries/Debug/Runtime --join 127.0.0.1:7777
```

`--host` listens on a port (7777 by default). `--join` connects to any peer already playing. Add
`--password something` on every peer to keep strangers out. That peer
introduces the newcomer to everyone else, so it does not matter whom you join. Try the Sample: start
two runtimes, click into each window and move with WASD.

To share an entity, add a **Network** component:

- The peer that owns an entity sends its transform `SendRate` times a second. Everyone else glides
  toward it.
- Turn on **OnePerPeer** for the player. Each peer swaps the scene's copy for its own when the session
  starts, and sees everyone else's.
- Entities your game spawns while playing belong to the peer that spawned them. They show up on every
  peer, go away when destroyed, and leave with their owner.
- Entities from the scene belong to the authority, the peer with the lowest id. When it leaves, the
  next one takes over.
- Game systems should leave other peers' entities alone. Skip any entity whose
  `NetworkComponent::Remote` is true, like `PlayerSystem` in the Sample does.

For your own messages, use the session from the `NetworkSystem`:

```cpp
NetSession& session = world.AddSystem<NetworkSystem>()->GetSession();
session.Start(7777);
session.Broadcast(NetChannel::Reliable, data, size);
```

Messages go on one of three channels:

| Channel | Use for |
|---|---|
| `Reliable` | Events that must arrive: always delivered, exactly once and in order |
| `Sequenced` | State that is replaced often: may be lost, and older ones are dropped |
| `Unreliable` | Anything that can be lost: may arrive out of order |

How it works, bottom to top:

- `UdpSocket` (`src/HAL/PlatformSocket.h`) is a non blocking IPv4 UDP socket.
- `NetConnection` is the reliability layer. Each packet acks the newest one received plus the 32
  before it. Reliable messages are resent when their packet goes unacked for longer than about the
  round trip time, and are put back in order on arrival. It also tracks round trip time and packet
  loss.
- `NetSession` runs the peers: handshakes, introductions, keepalives, timeouts and goodbyes. Both
  sides of a new pair say hello at once, which also gets through most home routers.
- `NetCrypto` encrypts. Each session makes fresh X25519 keys, and every pair of peers swaps public
  keys in its handshake. Each pair derives one key per direction, and every packet after that is
  sealed with XChaCha20-Poly1305 ([Monocypher](https://monocypher.org)). Packets that were changed,
  forged or recorded and played back are dropped. A peer only counts as joined once its packets
  decrypt.
- `NetworkSystem` replicates entities on top.

Limits:

- A message holds about 1000 bytes. Spawns of larger entities are split automatically, but your
  own messages are not.
- Without a password, encryption stops anyone listening in or tampering, but not someone who sits
  in the middle from the very first packet. With `SetPassword` (or `--password`), only peers that
  know the password can connect.
- No cheat protection. Peers that are in a session trust each other.
- The handshake itself is not encrypted, so anyone can see that two addresses are talking, and their
  peer ids and public keys.
- No relay server, so peers behind strict NATs cannot reach each other.
- At most 16 peers.

### Jobs

`FP::JobSystem` spreads work over one worker thread per core. Jobs run on fibers, small stacks the
engine switches between in user space, so a job that waits on other jobs parks its fiber and the
thread moves on instead of blocking. A fiber switch costs tens of nanoseconds where starting an OS
thread costs tens of microseconds.

```cpp
#include "Core/Jobs/JobSystem.h"

FP::JobCounter counter;
for (int32 i = 0; i < chunkCount; ++i)
    FP::JobSystem::Run([i]() { ProcessChunk(i); }, &counter);
FP::JobSystem::Wait(counter);

FP::JobSystem::ParallelFor(enemyCount, 32, [&](int32 i) { UpdateEnemy(i); });
```

- Jobs can run and wait on more jobs. Waiting inside a job never ties up a thread.
- The editor and runtime start the system for you. The main thread helps run jobs while it waits.
- Job lambdas hold up to 48 bytes of captures. Capture large data by reference.
- A job can resume on a different thread after a wait, so avoid `thread_local` inside jobs.
- Never spin inside a job waiting for the main thread. It may be the thread running that job.
- Systems update on the main thread. Use jobs for work that does not touch the world from many
  jobs at once, such as math over arrays or loading.

### Editor controls

Open **Settings > Engine Settings...** for color-blind assistance. Choose None, Protanopia,
Deuteranopia, Tritanopia, Protanomaly, Deuteranomaly, Tritanomaly, Achromatopsia or Achromatomaly,
then adjust **Strength** from 0 to 1. Changes preview live; **Save** keeps them in your per-user
engine preferences across scenes, projects and editor/runtime restarts. **Cancel** restores the
previous values. These preferences are independent of scene files and scene undo/redo.

Use **Entity > Post Processing...** for scene FXAA and bloom. **BloomIntensity** controls glow
(0-10), and **BloomThreshold** selects its minimum HDR brightness before exposure (default 1).
Both effects default to off. Save the scene to keep them. If more than one entity has PostProcess,
the earliest created entity supplies the settings. Bloom is bypassed in Unlit and Wireframe views.

The color modes assist contrast rather than simulate a deficiency. The first six redistribute
lost color contrast using [Machado's linear RGB model](https://www.inf.ufrgs.br/~oliveira/pubs_files/CVD_Simulation/CVD_Simulation.html);
the achromatic modes use a heuristic hue-to-brightness mapping. Check with affected players and
use shapes or patterns for critical cues. Game UI and editor gizmos retain their own colors and
stay sharp above post-processing. Run `Tests EngineSettings` and `Tests PostProcess` for persistence,
scene independence, GPU effects, all modes, resizing and scene replacement checks.

| Action | Control |
|---|---|
| Fly | Hold the right button, then WASD to move, Q and E for down and up, Shift to go faster. The wheel sets the speed while flying |
| Look around (3D) | Right drag |
| Pan | 2D: right or middle drag. 3D: middle drag or Shift right drag |
| Zoom | Mouse wheel |
| Orbit (3D) | Alt left drag |
| Toggle 3D view | Ctrl+3 or the Perspective pill. The editor opens in 3D |
| View mode | Viewport pill, or Alt+4 Lit, Alt+3 Unlit, Alt+2 Wireframe |
| Rename / delete asset | F2 / Delete in the content browser |
| Focus selection | F |
| Select / move | Left click / left drag along the view plane |
| Gizmo | Q select, W move, E rotate, R scale, or the buttons over the viewport. Drag an arrow, ring, box or plane square |
| World or local axes | Globe button next to the gizmo buttons |
| Grid snap | Grid pill over the viewport. Snaps moves to the grid and turns to 15 degrees. Shift flips snapping while dragging |
| Create sprite | Ctrl+Shift+A |
| Copy / paste / duplicate | Ctrl+C / Ctrl+V / Ctrl+D, children included |
| Delete | Delete or Backspace |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Play | Ctrl+P |
| Pause / resume | Ctrl+Shift+P or the toolbar button. The game camera stays visible |
| Step one frame | F10 while paused |
| System profiler | Profiler button in the status bar or Window > Profiler |
| New script / reload scripts | Scripts menu / Ctrl+R |
| Save | Ctrl+S |

### Smoke tests

```bash
Projects/Sample/Binaries/Debug/Tests
Projects/Sample/Binaries/Debug/Runtime --screenshot runtime.bmp
Projects/Sample/Binaries/Debug/Runtime --host 7777 --screenshot host.bmp
Projects/Sample/Binaries/Debug/Runtime Scenes/Physics.scene --wait 5 --screenshot physics.bmp
Projects/Sample/Binaries/Debug/Editor --play --screenshot editor.png
Projects/Sample/Binaries/Debug/Editor --scene Scenes/Lighting2D.scene --select "Glass Pane" --screenshot lit.png
Projects/Sample/Binaries/Debug/Editor --scene Scenes/Showcase3D.scene --3d --viewmode wireframe --screenshot 3d.png
Projects/Sample/Binaries/Debug/Editor --build-scripts
Projects/Sample/Binaries/Debug/Editor --browse Materials --asset Materials/Bricks.material --screenshot browser.png
Projects/Sample/Binaries/Debug/Editor --drop Textures/Bricks_AO.png --undo --redo --screenshot undo.png
Projects/Sample/Binaries/Debug/Editor --new-project MyGame
```

### Skybox and environment lighting

Create a **Skybox** from **Place Entities > Lights** or the **Entity** menu, then choose its
**Texture** in Details. Use a 2:1 equirectangular panorama under `Content`, up to 8192 pixels wide.
Radiance `.hdr` files retain values above white. PNG, JPG, TGA and BMP panoramas are read as sRGB.

The panorama supplies the visible sky, diffuse light and roughness filtered reflections on lit
materials and glass. **Intensity** scales the sky and lighting together. **Rotation** turns both
around Y. Turn off **Visible** to keep the lighting while showing the camera background, or turn off
**Enabled** to disable both. The first enabled Skybox in creation order is used. Existing ambient
and direct lights add to the environment lighting.

Open `Scenes/Environment.scene` to compare gold, plastic and glass with no direct or ambient lights.
The original sunset panorama and demo regenerate with `python3 Scripts/GenerateEnvironmentDemo.py`.
Run `Projects/Sample/Binaries/Debug/Tests Environment` for the environment tests.

Panoramas are filtered to at most 512 by 256 for rendering. Diffuse light uses nine spherical
harmonic coefficients and reflections use GGX filtered levels. Changes to the panorama reload it
automatically. Loading and filtering happen on first use and can briefly pause rendering.
Environment lighting does not cast shadows or capture nearby objects. Use direct lights for shadows
and material ambient occlusion maps for local detail. Missing or invalid panoramas use the camera
background and existing lights.

## Rendering

Rendering uses SDL_GPU. macOS runs on Metal and Windows runs on Vulkan, so Windows machines need a
GPU driver with Vulkan support (any recent NVIDIA, AMD or Intel driver).

The engine ships fixed base shaders. Games pick one per material and set its textures and values.
Game code cannot add shaders yet.

Shader sources live in `src/Engine/Shaders`. The compiled SPIR-V and MSL in `Engine/Content/Shaders` are
committed, so only engine developers who change a shader need the tools:

```bash
brew install glslang spirv-cross
```

```bash
Scripts/CompileShaders.sh
```

On Windows install the [Vulkan SDK](https://vulkan.lunarg.com) and run `Scripts\CompileShaders.ps1`.

### Meshlets and visibility debugging

Every mesh upload (primitives, OBJ, and glTF) builds clusters using meshoptimizer v0.25.
Clusters have at most 32 vertices and 60 triangles, preserve material sections and triangle
winding, and keep original vertex indices so skeletal weights remain aligned. Meshes are cached
by path; uploading a shared mesh builds its clusters once. The smaller clusters give occlusion
tighter bounds, trading more cluster metadata and indirect commands for finer rejection.

Static opaque meshes use compute frustum and occlusion culling followed by indexed indirect
main-pass draws. A current-frame depth prepass feeds an 8x8 max-depth tile buffer. A cluster is
occluded only when every tile touched by its projected bounds is nearer than the entire cluster.
Near-plane crossings, partially filled edge tiles, and very large projected bounds fail open.
This avoids stale visibility after camera/object movement and supports orthographic cameras.
The depth prepass still submits full occluder geometry; the counters describe the main pass,
not total frame geometry or a guaranteed speedup. Shadows retain their existing draws.

Open **Window > Nanite / Meshlet Debugger** for an ImGui window with:

- Enable, frustum, and compute occlusion toggles to compare with full mesh rendering.
- **Color meshlets** for stable colors per cluster.
- **Show outside camera** (on when the debugger opens) widens the observer view by 1.35x while
  culling continues from the original camera. The yellow frame marks that camera's frustum, so
  you can see meshlets crossing its edges in real time. Adjust **Observer width** for more context;
  closing the debugger restores the normal viewport. Picking and gizmos use the wider view too.
- **Freeze culling camera** to hold visibility at a captured viewpoint while moving the editor camera.
  The yellow outline then shows the frozen camera's volume from your new viewpoint.
- A live viewport preview, plus asynchronous GPU counts of visible, frustum-rejected and occluded
  meshlets and main-pass triangles. Counts are from the last completed readback.

Open `Scenes/Meshlets.scene` in the Sample project and press Play to see the demo camera.
Nine spheres sit behind a movable wall, three sit in front, and two sit outside the camera.
In edit mode, hold right mouse and use WASD in the viewport to move the live camera. The yellow
frame and culling update together; meshlets outside the frame disappear while clusters that cross
its edges remain visible. Disable **Frustum culling** to compare the surrounding geometry.
Freeze the culling camera to inspect its results from another viewpoint, or move the wall to watch
occlusion update. Camera movement does not require freezing.
The feature works in the Runtime too; the debugger is editor-only. For a reproducible editor smoke test:

```bash
Projects/Sample/Binaries/Debug/Editor --scene Scenes/Meshlets.scene --3d --play --meshlet-debugger --meshlet-colors --screenshot /tmp/meshlets.png --wait 3
Projects/Sample/Binaries/Debug/Tests Meshlet
```

The screenshot command also saves the debugger as `/tmp/meshlets.png.meshlets.png`.

This is a meshlet visibility implementation, with no hierarchical LOD selection, streaming, or
virtualized geometry yet. Animated meshes build meshlets but keep full draws until deformation-aware
bounds are available. Glass and sprites keep their existing paths. Alpha-cutout materials are not
used as occluders, and wireframe disables occlusion. If compute pipelines or buffers cannot be
created, rendering falls back to ordinary indexed draws. The debug window uses the editor's
existing GPU-to-Qt readback approach; close it when measuring rendering performance.

The shader scripts build both Vulkan SPIR-V and Metal MSL, including `.comp` files. Compute sources
use `FP_METAL` for SDL's different Metal buffer binding order; only the MSL compilation defines it.
The committed Vulkan SPIR-V uses SDL's normal descriptor sets. See
[SDL compute binding conventions](https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline).

## Build targets

| Target | What it is |
|---|---|
| Engine | Static lib with `src/HAL`, `src/Core`, `src/CoreObject` and `src/Engine` |
| ImGui | Dear ImGui core, demo and SDL_GPU renderer backend from the docking branch |
| Game | The project's components and systems (`Source`) |
| Editor | Qt editor |
| Runtime | Standalone player for a scene |
| AssetTool | Imports metadata, validates assets and lists dependencies |
| PackageTool | Validates assets, cooks content and stages a Release game for distribution |
| GameScripts | Hot reloadable C++ scripts (`Source/Scripts`) |
| Tests | Unit tests. Run `Projects/Sample/Binaries/Debug/Tests [filter]` |
| EditorTests | Input actions editor tests. Run `Projects/Sample/Binaries/Debug/EditorTests` |

## Third party

| Library | Use | How it gets here |
|---|---|---|
| [SDL3](https://libsdl.org) | Windowing, input, GPU rendering and audio | Homebrew on macOS, `Scripts/Setup.ps1` on Windows |
| [Qt 6](https://qt.io) | Editor UI | Homebrew on macOS, Qt installer + `QT_DIR` on Windows |
| [EnTT](https://github.com/skypjack/entt) | ECS | Git submodule in `ThirdParty/entt` |
| [stb_image](https://github.com/nothings/stb) | PNG, JPG and TGA loading | Git submodule in `ThirdParty/stb` |
| [dr_mp3](https://github.com/mackron/dr_libs) (public domain or MIT-0) | MP3 decoding | Git submodule in `ThirdParty/dr_libs`, pinned to `bad753d5ade4f30e40fcf80a757210b650681f8f` |
| [Monocypher](https://monocypher.org) | Key exchange and encryption for network packets | Git submodule in `ThirdParty/monocypher` |
| [spdlog](https://github.com/gabime/spdlog) | Logging to the console, files and the editor | Git submodule in `ThirdParty/spdlog` |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) v0.25 (MIT) | Automatic meshlet construction and index cache optimization | Git submodule in `ThirdParty/meshoptimizer`, pinned to `6daea4695c48338363b08022d2fb15deaef6ac09` |
| [cgltf](https://github.com/jkuhlmann/cgltf) | glTF loading for skeletal meshes and animations | Git submodule in `ThirdParty/cgltf` |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 5.6 | Rigid body physics, collisions and raycasts | Git submodule in `ThirdParty/JoltPhysics`, built as the `Jolt` project |
| [Ultralight](https://ultralig.ht) 2.0 beta | Game UI in HTML, CSS and JavaScript | Binary SDK downloaded by `Scripts/GetUltralight.sh` or `.ps1` into `ThirdParty/Ultralight`. Optional: without it game UI draws nothing |
| [Dear ImGui](https://github.com/ocornut/imgui/tree/docking) | Immediate mode UI with docking and multi-viewport support | Git submodule in `ThirdParty/imgui`, tracking upstream `docking` |

The ImGui submodule includes the complete upstream repository: core, demo, backends, examples,
fonts, documentation and MIT license. Its commit is pinned by this repository. The `ImGui` static
library builds every core source including `imgui_demo.cpp` and is linked through `UseEngine`;
engine and game targets can include `imgui.h`. The SDL_GPU renderer backend is also compiled.
The meshlet debugger hosts ImGui in a Qt window, forwards mouse input, and renders into an offscreen
GPU target. The rest of the editor remains Qt. Other backends and examples are not built.
Run `Projects/Sample/Binaries/Debug/Tests ImGui` for the headless docking and demo smoke test.

## Known limitations

- The editor viewport copies each rendered frame back to Qt, which adds GPU synchronization and CPU overhead.
- Some file loaders still use `fopen`, so non-ASCII paths on Windows are not supported consistently.
- Asset scans and imports run synchronously. There is no general asynchronous asset streaming or residency budget.
- Dependency discovery cannot inspect paths built dynamically in code. Relative links in model and web files constrain asset moves.
- Packaging preserves source asset formats and includes all content without compression or unused-asset removal.
- Meshlets provide visibility culling, with no hierarchical LOD selection or virtualized geometry streaming. Animated meshes retain full draws.
- Games use the built-in material shaders. Custom game shaders are not supported yet. Lighting supports up to 16 lights and one shadow caster of each supported type.
- Glass renders after opaque objects, so overlapping glass does not accumulate refraction through every surface.
- Animation state machines are authored in C++. There is no animation graph editor, blend spaces, root motion or animation event authoring.
- Scene spawning creates copies, with no linked prefab instances or overrides.
- Networking trusts participating peers, supports up to 16 peers, and has no relay for strict NATs.
- Scene undo stores full snapshots and retains up to 100 steps. Large scenes can increase its memory and serialization cost.
- The job queue uses a lock.
- Editor grid and gizmo lines draw on top of scene geometry.
- Transform rotations are Euler angles and can gimbal lock under combined rotations.
