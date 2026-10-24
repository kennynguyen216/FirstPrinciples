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

`Setup.sh` installs premake, SDL3 and Qt with Homebrew. `Build.sh` builds the Sample project: makefiles
go in `Projects/Sample/Build` and everything builds into `Projects/Sample/Binaries/Debug`. Pass `release`
for an optimised build, and a project folder to build another one, like `Scripts/Build.sh debug Projects/MyGame`.
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

### C++ scripts

Scripts are per entity behaviour written in C++ in the project's `Source/Scripts`. They build into a separate
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
- Components still live in `Source` and are registered in `Game::RegisterComponents`. Keep new
  component types out of `Source/Scripts`, since data owned by the scripts library would vanish
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
sprites, so raise it to light their faces. Open the Sample project's `Content/Scenes/Lighting2D.scene` for an example of
every material type.

### 3D

Add a **Mesh** component to draw a `Cube`, `Sphere`, `Plane` or `Quad`, or pick an `.obj` file from
`Content` with the `...` button. Meshes use the same materials as sprites and default to `Lit`.
For a 3D game turn on `Perspective` on the camera and give it a position and rotation.
The editor opens in 3D. Press Ctrl+3 or the Perspective pill to switch to the flat 2D view. Sample's `Content/Scenes/Showcase3D.scene`
shows every material on meshes, including a refracting glass sphere.

`Transform.Rotation` is euler degrees as pitch (X), yaw (Y) and roll (Z). Cameras and lights point
down their local -Z axis.

### Editor layout

The editor follows Unreal Engine 5's layout and dark theme, using this engine's own terms.

- **Place Entities** on the left: click an entry to create it, or drag it into the viewport to place it
  under the mouse. The icons on top filter by category.
- **Viewport** in the middle, in a tab named after the open scene. The pills over it switch between
  Perspective and Top (2D), pick Lit, Unlit or Wireframe, and turn grid snap on with a grid size.
- **Details** and **Hierarchy** share the panel on the right as tabs. Details shows the selection's components in collapsible sections. Add components with the Add
  button, remove one with the x on its header, and filter fields with the search box.
- **Hierarchy** lists every entity under the scene with its type, plus a search box.
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
| Duplicate / delete | Ctrl+D / Delete |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Play | Ctrl+P |
| New script / reload scripts | Scripts menu / Ctrl+R |
| Save | Ctrl+S |

### Smoke tests

```bash
Projects/Sample/Binaries/Debug/Tests
Projects/Sample/Binaries/Debug/Runtime --screenshot runtime.bmp
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
| Game | The project's components and systems (`Source`) |
| Editor | Qt editor |
| Runtime | Standalone player for a scene |
| GameScripts | Hot reloadable C++ scripts (`Source/Scripts`) |
| Tests | Unit tests. Run `Projects/Sample/Binaries/Debug/Tests [filter]` |

## Third party

| Library | Use | How it gets here |
|---|---|---|
| [SDL3](https://libsdl.org) | Windowing, input, 2D rendering | Homebrew on macOS, `Scripts/Setup.ps1` on Windows |
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
