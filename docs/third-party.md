# Third-party dependencies

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The engine uses established libraries for platform services, editor widgets, data storage and
specialized formats. This page records their roles, acquisition paths and the existing dependency
version notes.

## Usage and behavior

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
engine and game targets can include `imgui.h`. The SDL_GPU renderer backend is also compiled. The
meshlet debugger hosts ImGui in a Qt window, forwards mouse input, and renders into an offscreen GPU
target. The rest of the editor remains Qt. Other backends and examples are not built. Run
`Projects/Sample/Binaries/Debug/Tests ImGui` for the headless docking and demo smoke test.

## How it works

Git submodules pin source dependencies to the repository’s selected revisions. Setup scripts install
system dependencies or download the optional UI SDK. Premake builds the relevant library targets and
links them into the tools that use them. Consult each dependency’s own license before
redistribution.

Source: [.gitmodules](../.gitmodules), [premake5.lua](../premake5.lua),
[Setup.sh](../Scripts/Setup.sh), [Setup.ps1](../Scripts/Setup.ps1).

## Related guides

[Setup](building.md), [Redistribution](packaging.md), [Ultralight UI](game-ui.md).
