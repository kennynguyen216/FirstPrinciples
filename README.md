# FirstPrinciples

A small C++20 game engine with an ECS, a Qt editor and SDL3 rendering. Runs on macOS and Windows.

## Setup

Clone with submodules so EnTT and stb come along:

```bash
git clone --recursive https://github.com/kennynguyen216/FirstPrinciples.git
```

In an existing clone run `git submodule update --init`.

### macOS

```bash
Scripts/Setup.sh
Scripts/Build.sh
```

`Setup.sh` installs premake, SDL3 and Qt with Homebrew. `Build.sh` generates makefiles in `Build/`
and builds everything into `Binaries/Debug`. Pass `release` for an optimised build.
For Xcode run `Scripts/GenerateXcode.sh` instead.

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

5. Open the generated solution in `Build` (`FirstPrinciples.sln` for VS 2022 or
   `FirstPrinciples.slnx` for VS 2026), set `Editor` as the startup project and build.

`GenerateProjects.bat` selects the installed VS 2022 or VS 2026 C++ toolset. With Build Tools only,
run `msbuild Build\FirstPrinciples.slnx /m /p:Configuration=Debug /p:Platform=x64` from its Developer
PowerShell or Developer Command Prompt (use `.sln` for VS 2022). Use `Configuration=Release` for an optimised build.
Run `powershell -ExecutionPolicy Bypass -File Scripts\TestGenerateProjects.ps1` to check Windows project generation.

## Making your game

Game code lives in `Game/`. The editor and the runtime both link it.

1. **Components** are plain structs. Fields can be `bool`, `int32`, `float`, `Vec2`, `Color` or `String`.

   ```cpp
   struct HealthComponent
   {
       int32 Value = 100;
   };
   ```

2. **Register** them in `Game::RegisterComponents` (`Game/Game.cpp`) so the Details panel and scene files see them.

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

5. **Scenes** live in `Content/Scenes`. Build them in the editor and press Play to run your systems.
   Stop restores the scene. `Runtime` plays `Game::StartScene` or a scene passed on the command line.

Textures are `.png`, `.jpg`, `.tga` or `.bmp` files under `Content/`. Use the `...` button next to a texture
field to pick one.

### Audio

Use **File > Import Audio...** to copy a WAV into `Content/Audio`, or copy it there yourself.
Add an **AudioSource** component to an entity and use its `Clip` picker to select the file.
Turn on **PlayOnStart** to hear it when the scene starts, and **Loop** for ambience. The inspector
provides preview, reload and playback controls. Editor **Stop** stops game audio and restores the scene.
Imports validate a staged copy and never overwrite an existing destination.

Supported clips are uncompressed PCM16 or float32 WAV, mono or stereo, at 8-96 kHz. Clips must be
at most 60 seconds, 64 MiB on disk and 32 MiB decoded. Decoded clips share a bounded 128 MiB cache.
Audio uses SDL3; no additional audio library or asset conversion is required. Missing output hardware
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

Open `Content/Scenes/AudioDemo.scene` and press Play. Move the listener with WASD/arrows and press
Space for overlapping clicks. Demo WAVs are original generated sounds; regenerate and check them
with `python Scripts/GenerateAudioFixtures.py`. Run `Binaries/Debug/Tests Audio` for audio tests.

The first version uses resident WAVs and simple stereo panning. Compressed/streamed music, HRTF,
occlusion, Doppler and effects are deferred. Dense mixes can hard-clip, and an immediate Stop can
click.

### C++ scripts

Scripts are per entity behaviour written in C++ in `Game/Scripts`. They build into a separate
`GameScripts` library that the editor reloads while it runs, so you can change code without
restarting, even in the middle of play.

```cpp
#include "Engine/Scripting/Script.h"

using namespace FP;

class Rotator : public Script
{
public:
    Vec3 DegreesPerSecond{ 0, 90, 0 };

    void OnUpdate(float deltaTime) override
    {
        GetTransform().Rotation += DegreesPerSecond * deltaTime;
    }
};

FP_SCRIPT(Rotator)
{
    fields.Field("DegreesPerSecond", &Rotator::DegreesPerSecond);
}
```

- **Scripts > New Script...** writes a starter file and opens it in a tab next to the viewport.
- Add a **Scripts** component to an entity, then **Add Script**. An entity can have any number.
- Fields listed in `FP_SCRIPT` show in the Details panel, save with the scene and keep their values
  across hot reloads. Other members reset when scripts reload.
- Saving a script rebuilds and reloads it (turn this off with **Scripts > Auto Reload**).
  **Scripts > Reload Scripts** (Ctrl+R) forces it. Build output goes to the console, and double
  clicking an error opens the file at that line.
- Overrides: `OnStart`, `OnUpdate`, `OnStop` and `OnHotReload` (defaults to calling `OnStart`).
  Helpers: `GetTransform()`, `Get<T>()`, `TryGet<T>()`, `Add<T>()`, `FindEntity(name)`, `DestroySelf()`.
- Components still live in `Game/` and are registered in `Game::RegisterComponents`. Keep new
  component types out of `Game/Scripts`, since data owned by the scripts library would vanish
  when it reloads.

The runtime loads the same library at startup. Building from the command line works as before,
and the `GameScripts` project builds along with the editor.

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
sprites, so raise it to light their faces. Open `Content/Scenes/Lighting2D.scene` for an example of
every material type.

### 3D

Add a **Mesh** component to draw a `Cube`, `Sphere`, `Plane` or `Quad`, or pick an `.obj` file from
`Content` with the `...` button. Meshes use the same materials as sprites and default to `Lit`.
For a 3D game turn on `Perspective` on the camera and give it a position and rotation.
Press **3D View** (Ctrl+3) in the editor to orbit around the scene. `Content/Scenes/Showcase3D.scene`
shows every material on meshes, including a refracting glass sphere.

`Transform.Rotation` is euler degrees as pitch (X), yaw (Y) and roll (Z). Cameras and lights point
down their local -Z axis.

### Editor layout

The editor follows Unreal Engine 5's layout and dark theme, using this engine's own terms.

- **Place Entities** on the left: click an entry to create it, or drag it into the viewport to place it
  under the mouse. The icons on top filter by category.
- **Viewport** in the middle, in a tab named after the open scene. The pills over it switch between
  Perspective and Top (2D), pick Lit, Unlit or Wireframe, and turn grid snap on with a grid size.
- **Details** shows the selection's components in collapsible sections. Add components with the Add
  button, remove one with the x on its header, and filter fields with the search box.
- **Outliner** lists every entity under the scene with its type, plus a search box.
- **Content Browser** and **Output Log** share the bottom. The status bar has buttons for both and a
  console box: type `help` for the commands (play, stop, save, undo, redo, gc, 2d, 3d, viewmode,
  select, create).

### Content browser

The **Content Browser** tab next to the **Output Log** at the bottom of the editor shows everything in
`Content` plus the scripts in `Game/Scripts`. Textures show thumbnails and materials show their color.

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
| Pan | 2D: right or middle drag. 3D: middle drag or Shift right drag |
| Zoom | Mouse wheel |
| Orbit (3D) | Right drag or Alt left drag |
| Toggle 3D view | Ctrl+3 or the Perspective pill |
| View mode | Viewport pill, or Alt+4 Lit, Alt+3 Unlit, Alt+2 Wireframe |
| Rename / delete asset | F2 / Delete in the content browser |
| Focus selection | F |
| Select / move | Left click / left drag along the view plane |
| Grid snap | Grid pill over the viewport. Shift flips snapping while dragging |
| Create sprite | Ctrl+Shift+A |
| Duplicate / delete | Ctrl+D / Delete |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Play | Ctrl+P |
| New script / reload scripts | Scripts menu / Ctrl+R |
| Save | Ctrl+S |

### Smoke tests

```bash
Binaries/Debug/Tests
Binaries/Debug/Runtime --screenshot runtime.bmp
Binaries/Debug/Editor --play --screenshot editor.png
Binaries/Debug/Editor --scene Scenes/Lighting2D.scene --select "Glass Pane" --screenshot lit.png
Binaries/Debug/Editor --scene Scenes/Showcase3D.scene --3d --viewmode wireframe --screenshot 3d.png
Binaries/Debug/Editor --build-scripts
Binaries/Debug/Editor --browse Materials --asset Materials/Bricks.material --screenshot browser.png
Binaries/Debug/Editor --drop Textures/Bricks_AO.png --undo --redo --screenshot undo.png
```

## Rendering

Rendering uses SDL_GPU. macOS runs on Metal and Windows runs on Vulkan, so Windows machines need a
GPU driver with Vulkan support (any recent NVIDIA, AMD or Intel driver).

The engine ships fixed base shaders. Games pick one per material and set its textures and values.
Game code cannot add shaders yet.

Shader sources live in `src/Engine/Shaders`. The compiled SPIR-V and MSL in `Content/Engine/Shaders` are
committed, so only engine developers who change a shader need the tools:

```bash
brew install glslang spirv-cross
```

```bash
Scripts/CompileShaders.sh
```

On Windows install the [Vulkan SDK](https://vulkan.lunarg.com) and run `Scripts\CompileShaders.ps1`.

## Projects

| Project | What it is |
|---|---|
| Engine | Static lib with `src/HAL`, `src/Core`, `src/CoreObject` and `src/Engine` |
| Game | The game's components and systems (`Game/`) |
| Editor | Qt editor |
| Runtime | Standalone player for a scene |
| GameScripts | Hot reloadable C++ scripts (`Game/Scripts`) |
| Tests | Unit tests. Run `Binaries/Debug/Tests [filter]` |

## Third party

| Library | Use | How it gets here |
|---|---|---|
| [SDL3](https://libsdl.org) | Windowing, input, GPU rendering and audio | Homebrew on macOS, `Scripts/Setup.ps1` on Windows |
| [Qt 6](https://qt.io) | Editor UI | Homebrew on macOS, Qt installer + `QT_DIR` on Windows |
| [EnTT](https://github.com/skypjack/entt) | ECS | Git submodule in `ThirdParty/entt` |
| [stb_image](https://github.com/nothings/stb) | PNG, JPG and TGA loading | Git submodule in `ThirdParty/stb` |

## Known limitations

- The editor viewport renders offscreen and copies each frame back to Qt. Fine for editing, but slower than a native view.
- File paths go through `fopen`, so non ASCII paths on Windows are not supported yet.
- No undo yet.
- The job queue uses a lock. A lock free queue will help when many tiny jobs are queued at once.
- Glass draws after everything else, so glass behind glass is not refracted twice.
- No shadows yet.
- Editor grid and gizmo lines draw on top of everything.
- Rotations are euler angles, so very large combined rotations can gimbal lock.
