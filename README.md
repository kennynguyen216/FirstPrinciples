# FirstPrinciples

A small C++20 game engine with an ECS, a Qt editor and SDL3 rendering. Runs on macOS and Windows.

## Setup

Clone with submodules so EnTT comes along:

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

Sprite textures are `.bmp` files under `Content/`.

### Editor controls

| Action | Control |
|---|---|
| Pan | Right or middle drag |
| Zoom | Mouse wheel |
| Select / move | Left click / left drag (hold Shift to snap) |
| Create sprite | Ctrl+Shift+S |
| Duplicate / delete | Ctrl+D / Delete |
| Play | Ctrl+P |
| Save | Ctrl+S |

### Smoke tests

```bash
Binaries/Debug/Tests
Binaries/Debug/Runtime --screenshot runtime.bmp
Binaries/Debug/Editor --play --screenshot editor.png
```

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

## Known limitations

- Textures load `.bmp` only. PNG needs SDL_image.
- The editor viewport uses the software renderer, so rotated sprites can show faint seams there.
- File paths go through `fopen`, so non ASCII paths on Windows are not supported yet.
- No undo yet.
