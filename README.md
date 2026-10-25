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

Open `Projects/Sample/Binaries/Debug/Editor.app` after building (or `Release` for a release build).
WAV/MP3 playback, streaming and proximity voice use the same SDL3 audio backend on Apple Silicon
and Intel Macs. Enable voice in the engine before macOS asks for microphone access; allow it under
**System Settings > Privacy & Security > Microphone** if you previously denied it. Runtime also
includes the microphone usage description; a Terminal launch may use Terminal's permission.
Intel Macs build without the optional Ultralight HTML game UI because its pinned SDK is ARM-only.
The macOS workflow builds Debug and Release and runs the tests on native Apple Silicon and Intel runners.

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

4. **Input** works the same in the editor and the runtime. Use `FP::Input::IsKeyDown(SDL_SCANCODE_SPACE)`
   or `FP::Input::GetMoveAxis()`.

5. **Scenes** live in the project's `Content/Scenes`. Build them in the editor and press Play to run your systems.
   Stop restores the scene. `Runtime` plays `Game::StartScene` or a scene passed on the command line.

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

Open `Scenes/Acoustics3D.scene` in the editor and press Play, or run:

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

Compressed music streaming and HRTF remain deferred.

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
| Physics | `AddForce(force)`, `AddImpulse(impulse)`, `AddForceAtPoint`, `AddImpulseAtPoint`, `GetVelocity()`, `SetVelocity(velocity)`, `GetMass()`, `GetPhysics()` for the rest |
| Characters | `Move(velocity)` every frame, `Jump(speed)`, `IsGrounded()` |
| Particles | `EmitParticles(count)` bursts from this entity's emitter, or another's |
| Physics queries | `Raycast(origin, direction, distance, hit)`, `SphereCast(origin, radius, direction, distance, hit)`, `RaycastFromCamera(distance, hit)`, `OverlapSphere(center, radius)`, `AddExplosion(center, radius, impulse)` |
| Cameras | `SetActiveCamera(entity or name)` (this entity by default), `GetActiveCamera()`, `IsActiveCamera()` |
| Physics events | `OnCollisionEnter(other, collision)`, `OnCollisionExit(other)`, `OnTriggerEnter(other)`, `OnTriggerExit(other)` |
| Math | `Clamp`, `Lerp`, `InverseLerp`, `MoveTowards`, `Damp`, `Random::Range`, `Random::Chance`, `Random::Direction` |
| Input and logs | `Input::IsKeyDown(SDL_SCANCODE_SPACE)`, `Input::GetMoveAxis()`, `Input::GetMouseDelta()`, `Input::SetCursorMode(mode)`, `FP_LOG("...")` |

**Mouse and cameras**

While the game plays the mouse is hidden and held in place, so `Input::GetMouseDelta()` drives
mouse look. Call `Input::SetCursorMode(CursorMode::Visible)` in `OnStart` for menus and HUDs with
buttons, or `Hidden` to draw your own pointer. In the editor Esc frees the mouse and a click on
the viewport takes it back.

The game sees through the first camera with `Primary` on. `SetActiveCamera` cuts to another one
by turning `Primary` off everywhere else.

`SpawnScene` copies every entity in a scene file with new ids, so any scene works as a prefab and
can be spawned many times. Timers stop when the script stops or reloads.

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
that plays with no code in any project. Its **PlayerInput** component walks it with WASD, runs with
Shift, jumps with Space and turns it with the mouse. Remove PlayerInput to drive it from a script:

```cpp
void OnUpdate(float) override
{
    const Vec2 axis = Input::GetMoveAxis();
    Move(Vec3(axis.X, 0, -axis.Y) * 5); // meters per second, call it every frame
    if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
        Jump(6);                        // only works on the ground
}
```

`AirControl` sets how much `Move` steers mid jump. `SetVelocity` and `AddImpulse` knock a
character about, and explosions push it too. `Content/Scenes/Character.scene` has a third person
player with stairs, a walkable and a too steep ramp, a lift, crates and a zone
that counts the player walking in.

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

The first `DirectionalLight` with `CastShadows` on casts shadows from meshes and skeletal meshes onto
anything using the `Lit` shader. Glass sections do not cast. It is kept cheap:

- One small shadow map, `ShadowResolution` pixels square (1024 by default) in 16 bit depth.
- It covers only `ShadowDistance` world units just ahead of the camera, so nearby shadows get the
  detail. Meshes outside that box are skipped before drawing.
- Edges are softened by four hardware filtered taps instead of many manual ones.
- The box snaps to whole shadow pixels so edges stay still while the camera moves.
- Scenes with no meshes, and the Unlit and Wireframe view modes, skip the pass.

Lower `ShadowResolution` for speed and softer edges, or lower `ShadowDistance` for sharper shadows
close up. `ShadowStrength` fades them. Point lights do not cast shadows.

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
- **Content Browser** and **Output Log** share the bottom. The status bar has buttons for both and a
  console box: type `help` for the commands (play, stop, save, undo, redo, gc, 2d, 3d, viewmode,
  select, create).

### Content browser

The **Content Browser** tab next to the **Output Log** at the bottom of the editor shows everything in
the project's `Content` plus its scripts in `Source/Scripts`. Textures show thumbnails and materials show their color.

- Double click a scene to open it or a script to edit it in a tab. Click a material to edit it in
  the Details panel, or a texture to preview it.
- Drag a texture into the viewport to make a sprite, an `.obj` to make a mesh, or a material onto a
  sprite or mesh to assign it. Drag any asset onto a matching Details field to fill it in.
- **Add** or a right click makes a New Folder, Material, Scene or Script. **Import** copies files in,
  and **Save All** saves the scene, materials and script tabs. Back, forward and the folder path
  above the files move between folders.
- Right click an asset to Rename (F2), Duplicate, Delete, Copy Path or Show in Finder or Explorer.
- Renaming rewrites references in every scene and material. Delete moves files to the system Trash.
- The browser refreshes on its own when files change, and the Output Log comes forward when a script
  build fails.

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

## Build targets

| Target | What it is |
|---|---|
| Engine | Static lib with `src/HAL`, `src/Core`, `src/CoreObject` and `src/Engine` |
| ImGui | Dear ImGui core, widgets, tables, drawing and demo from the docking branch |
| Game | The project's components and systems (`Source`) |
| Editor | Qt editor |
| Runtime | Standalone player for a scene |
| GameScripts | Hot reloadable C++ scripts (`Source/Scripts`) |
| Tests | Unit tests. Run `Projects/Sample/Binaries/Debug/Tests [filter]` |

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
| [cgltf](https://github.com/jkuhlmann/cgltf) | glTF loading for skeletal meshes and animations | Git submodule in `ThirdParty/cgltf` |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 5.6 | Rigid body physics, collisions and raycasts | Git submodule in `ThirdParty/JoltPhysics`, built as the `Jolt` project |
| [Ultralight](https://ultralig.ht) 2.0 beta | Game UI in HTML, CSS and JavaScript | Binary SDK downloaded by `Scripts/GetUltralight.sh` or `.ps1` into `ThirdParty/Ultralight`. Optional: without it game UI draws nothing |
| [Dear ImGui](https://github.com/ocornut/imgui/tree/docking) | Immediate mode UI with docking and multi-viewport support | Git submodule in `ThirdParty/imgui`, tracking upstream `docking` |

The ImGui submodule includes the complete upstream repository: core, demo, backends, examples,
fonts, documentation and MIT license. Its commit is pinned by this repository. The `ImGui` static
library builds every core source including `imgui_demo.cpp` and is linked through `UseEngine`;
engine and game targets can include `imgui.h`. Backends and examples remain available in the
submodule but are not compiled into the engine. Window, input and renderer integration is still
required before displaying ImGui in an application. The editor currently uses Qt.
Run `Projects/Sample/Binaries/Debug/Tests ImGui` for the headless docking and demo smoke test.

## Known limitations

- The editor viewport renders offscreen and copies each frame back to Qt. Fine for editing, but slower than a native view.
- File paths go through `fopen`, so non ASCII paths on Windows are not supported yet.
- No undo yet.
- The job queue uses a lock. A lock free queue will help when many tiny jobs are queued at once.
- Glass draws after everything else, so glass behind glass is not refracted twice.
- No shadows yet.
- Editor grid and gizmo lines draw on top of everything.
- Rotations are euler angles, so very large combined rotations can gimbal lock.
