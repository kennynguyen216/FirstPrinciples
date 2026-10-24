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

2. **Register** them in `Game::RegisterComponents` (`Game/Game.cpp`) so the inspector and scene files see them.

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

### Materials and lighting

A sprite draws unlit with its `Tint` and `Texture` until it gets a **Material** component. That points
at a shared `.material` file in `Content/Materials`. Select the entity and the inspector shows the
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

### Editor controls

| Action | Control |
|---|---|
| Pan | Right or middle drag |
| Zoom | Mouse wheel |
| Select / move | Left click / left drag (hold Shift to snap) |
| Create sprite | Ctrl+Shift+A |
| Duplicate / delete | Ctrl+D / Delete |
| Play | Ctrl+P |
| Save | Ctrl+S |

### Smoke tests

```bash
Binaries/Debug/Tests
Binaries/Debug/Runtime --screenshot runtime.bmp
Binaries/Debug/Editor --play --screenshot editor.png
Binaries/Debug/Editor --scene Scenes/Lighting2D.scene --select "Glass Pane" --screenshot lit.png
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
| Tests | Unit tests. Run `Binaries/Debug/Tests [filter]` |

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
- Glass draws after everything else, so glass behind glass is not refracted twice.
- No shadows yet.
