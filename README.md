# PlutoGE

PlutoGE is a work-in-progress 3D game engine, editor, and standalone runtime written in C++20. It combines an OpenGL renderer, entity/component scene system, asset pipeline, physics, audio, runtime UI, and hosted .NET 8 C# scripting in one CMake-based repository.

The project currently has a Windows-first development and export workflow. It is under active development, so file formats and public engine APIs may change.

## Contents

- [Highlights](#highlights)
- [Technology](#technology)
- [Requirements](#requirements)
- [Build and run](#build-and-run)
- [Create a project](#create-a-project)
- [Editor workflow](#editor-workflow)
- [C# scripting](#c-scripting)
- [Projects and assets](#projects-and-assets)
- [Run and export a game](#run-and-export-a-game)
- [Tests](#tests)
- [CMake options and presets](#cmake-options-and-presets)
- [Architecture](#architecture)
- [Repository layout](#repository-layout)
- [Troubleshooting](#troubleshooting)
- [Further documentation](#further-documentation)

## Highlights

### Editor

- Dockable ImGui editor with editor and game viewports
- Scene hierarchy, inspector, content browser, console, and profiler
- Translate, rotate, and scale gizmos with local/world modes and snapping
- Perspective and orthographic editor cameras
- Entity selection, parenting, copy/paste, duplication, deletion, and undo/redo
- Play-in-editor with configurable simulation speed
- Dedicated material, mesh, particle-system, shader-graph, animation-clip, and animation-graph editors
- Scene baking with fast, balanced, final, and custom quality settings
- Project settings for startup scene, script assembly, window size/title, VSync, editor camera, and post-processing

### Rendering

- OpenGL 4.3 core renderer with a deferred G-buffer pipeline
- Geometry, shadow, lighting, transparent, particle, ocean, physical-sky, volumetric-cloud, grid, runtime-UI, and post-process passes
- Directional, point, and spot lighting with shadow support
- Materials, textures, shader graphs, render targets, LODs, indirect drawing, and frustum culling
- Environment maps, image-based lighting capture volumes, and baked probe volumes
- Terrain, foliage, splines, skeletal animation, particles, oceans, and volumetric clouds
- Post-processing effects including bloom, tone mapping, color grading, auto exposure, SSAO, SSGI, SSR, TAA, FXAA, motion blur, depth of field, lens flare, volumetric fog, gamma correction, LPV/RSM lighting, and voxel cone tracing
- CPU/GPU render-pass timings and draw/submission statistics in the editor profiler

Some advanced rendering paths are experimental and may depend on scene setup, compatible hardware, or generated bake data.

### Gameplay systems

- Hierarchical entities with serializable native components
- Bullet-based rigidbodies, colliders, collision events, raycasts, and kinematic movement
- OpenAL Soft audio, with XAudio2 integration on Windows
- 2D and 3D sound emitters/listeners, looping, one-shots, and audio obstruction support
- Navigation meshes and navigation agents
- Runtime canvas, image, text, and button components
- Prefabs, tags, scene transitions, animation graphs/events, and scriptable data assets
- .NET 8 C# behaviours with editor-serialized fields and native component wrappers
- Multi-client networking with reliable messaging, targeted sends, broadcasts, and JSON/binary payloads

## Technology

| Area | Implementation |
|---|---|
| Language | C++20 engine/editor; C#/.NET 8 gameplay API |
| Build | CMake 3.10+ with CMake presets |
| Window/input | GLFW |
| Graphics | OpenGL 4.3 core, GLAD, GLM |
| Editor UI | Dear ImGui docking branch and ImGuizmo |
| Physics | Bullet |
| Audio | OpenAL Soft; XAudio2 on Windows |
| Networking | Managed .NET 8 TCP transport with main-thread event dispatch |
| Models | Assimp, tinygltf, meshoptimizer |
| Images | stb |
| Scripting host | `hostfxr` loading `PlutoGE.ScriptCore` |

Most third-party projects are acquired by CMake through `FetchContent`, so the first configure requires an internet connection.

## Requirements

The supported workflow in this repository is Windows with:

- A 64-bit Windows installation
- A GPU and driver supporting OpenGL 4.3
- [Git](https://git-scm.com/)
- [CMake](https://cmake.org/) 3.10 or newer
- Visual Studio 2022 with the **Desktop development with C++** workload
- The .NET 8 SDK for building gameplay scripts
- PowerShell for the shipping export helper

The repository also contains a `gcc` preset aimed at `C:\w64devkit`, but it is machine-specific. The MSVC presets are the portable starting point for a normal Windows checkout.

## Build and run

Clone and configure the MSVC build:

```powershell
git clone https://github.com/CameronAirlie/PlutoGE.git
cd PlutoGE
cmake --preset msvc
cmake --build --preset msvc-debug
```

The first configure downloads and configures the dependencies declared in [`third_party/CMakeLists.txt`](third_party/CMakeLists.txt), so it can take longer than later builds.

Run the editor:

```powershell
.\out\build\msvc\editor\Debug\PlutoGEEditor.exe
```

The same build also produces the development runtime:

```powershell
.\out\build\msvc\runtime\Debug\PlutoGERuntime.exe
```

If your generator places executables in a different subdirectory, find them with:

```powershell
Get-ChildItem .\out\build\msvc -Recurse -Filter PlutoGEEditor.exe
Get-ChildItem .\out\build\msvc -Recurse -Filter PlutoGERuntime.exe
```

### Build without a preset

For a custom generator or build directory:

```powershell
cmake -S . -B out/build/custom `
  -DPLUTO_BUILD_EDITOR=ON `
  -DPLUTO_BUILD_RUNTIME=ON `
  -DBUILD_TESTING=ON
cmake --build out/build/custom --config Debug
```

## Create a project

1. Start `PlutoGEEditor`.
2. Select **File → New Project…**.
3. Choose a location and a `.plutoproject` filename.
4. Build gameplay scripts from **Runtime → Build Scripts** after adding or changing C# files.
5. Press `F5` to enter play mode and `Shift+F5` to stop.
6. Save the project before creating a standalone build.

A new project is scaffolded approximately as follows:

```text
MyGame/
├── MyGame.plutoproject
├── MyGame.Scripts.csproj
└── Assets/
    ├── Managed/
    ├── Materials/
    ├── Meshes/
    ├── Scenes/
    │   └── Main.plutoscene
    └── Scripts/
```

The exact generated files grow as assets and scripts are added. The project manifest records the startup scene, asset registry, runtime window settings, managed assembly, and editor-specific settings.

## Editor workflow

The typical content loop is:

1. Import source content by dragging it into the content browser or using its import actions.
2. Drag generated assets into the scene or create entities from the scene hierarchy.
3. Configure transforms and components in the inspector.
4. Save reusable entity trees as prefabs.
5. Create C# behaviours in `Assets/Scripts`, build them, and attach them with a Script component.
6. Test in the game viewport with play-in-editor.
7. Set the startup scene and runtime window options in **Project Settings**.
8. Bake scene lighting/probes as needed, then export the game.

Useful shortcuts:

| Shortcut | Action |
|---|---|
| `F5` | Start play mode |
| `Shift+F5` | Stop play mode |
| `W`, `E`, `R` | Move, rotate, and scale gizmos while the viewport is focused |
| `F` | Focus the selected entity in the viewport |
| `Ctrl+Z`, `Ctrl+Y` | Undo and redo |
| `Ctrl+C`, `Ctrl+V`, `Ctrl+D` | Copy, paste, and duplicate the selected entity |
| `Delete` | Delete the selected entity |

Editor camera navigation uses `W/A/S/D` for planar movement, `Q/E` for vertical movement, Shift to move faster, and mouse controls in the viewport for looking, panning, and orbiting.

## C# scripting

PlutoGE hosts .NET 8 through `hostfxr`. Project scripts normally live in `Assets/Scripts`; the editor maintains a project `.csproj`, writes build output to `Assets/Managed`, reflects attachable classes, and reloads the assembly after a successful build.

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class Rotator : ScriptBehaviour
{
    [SerializedField] private float degreesPerSecond = 45.0f;

    public override void OnCreate()
    {
        Debug.Log($"Started {GameObject.Name}");
    }

    public override void OnUpdate(float deltaTime)
    {
        GameObject.Rotation += Vector3.UnitY * degreesPerSecond * deltaTime;
    }

    public override void OnDestroy()
    {
        Debug.Log($"Stopped {GameObject.Name}");
    }
}
```

Attachable classes derive from `ScriptBehaviour` and expose editor-editable values with `[SerializedField]`. The managed API includes:

- Entity lookup, tags, activation, transforms, destruction, and script messaging
- Mesh, camera, light, rigidbody, collider, animation, particle, audio, and runtime-UI wrappers
- Keyboard, mouse, cursor, and application state
- Raycasts, tagged raycasts, impulses/forces, and kinematic movement
- Prefab instantiation and deferred scene loading
- Scriptable objects and safe project/user-data storage
- `OnCreate`, `OnUpdate`, `OnLateUpdate`, `OnDestroy`, collision, and animation-event callbacks
- Reliable multi-client networking with raw binary, string, and JSON messages

Networking is available through `PlutoGE.ScriptCore.Networking`. Socket I/O runs
in the background while callbacks are dispatched by `Poll()` on the gameplay
thread:

```csharp
using PlutoGE.ScriptCore.Networking;

private readonly NetworkServer server = new();

public override void OnCreate()
{
    server.MessageReceived += message =>
        server.Broadcast(message.Channel, message.Payload.Span,
            exceptPeerId: message.PeerId);
    server.Start(7777);
}

public override void OnUpdate(float deltaTime)
{
    server.Poll();
}

public override void OnDestroy()
{
    server.Dispose();
}
```

See the full [C# scripting specification](docs/CSHARP_SCRIPTING.md) before authoring gameplay code. It documents supported serialized types and APIs; PlutoGE does not expose Unity APIs such as `MonoBehaviour`, `Transform`, coroutines, or `FixedUpdate`.
See [PlutoGE networking](docs/NETWORKING.md) for the transport design, usage
model, wire format, and planned replication layers.

## Projects and assets

Project-owned assets use stable references such as:

```text
project://Scenes/Main.plutoscene
project://Materials/Metal.plutomaterial
```

Built-in engine assets use the `engine://` scheme, for example:

```text
engine://builtin/mesh/cube
engine://builtin/material/default-shaded
```

Important PlutoGE formats include:

| Extension | Purpose |
|---|---|
| `.plutoproject` | Project manifest and settings |
| `.plutoscene` | Serialized scene |
| `.plutoprefab` | Reusable entity hierarchy |
| `.plutomesh` / `.plutomodel` | Imported mesh and model assets |
| `.plutomaterial` | Material asset |
| `.plutoshadergraph` | Shader graph |
| `.plutoanim` / `.plutoclip` | Animation references and clips |
| `.plutoanimgraph` | Animation state graph |
| `.plutoparticles` | Particle-system asset |
| `.plutopostprocess` | Post-process preset |
| `.plutoscriptable` | C# scriptable-object data |
| `.plutometa` | Asset metadata |
| `.plutopack` | Cooked standalone content container |

The model pipeline handles FBX and glTF/GLB source packages, generates discrete mesh/material/animation assets, and applies mesh optimization and LOD data. The asset system also recognizes OBJ meshes, common raster/HDR textures, WAV audio, and managed assemblies.

Generated mesh-import cache data is stored under `.plutoge-cache/` and is intentionally ignored by Git.

## Run and export a game

### Run a development project

Pass a project manifest directly to the runtime:

```powershell
.\out\build\msvc\runtime\Debug\PlutoGERuntime.exe `
  "C:\Projects\MyGame\MyGame.plutoproject"
```

The runtime loads the manifest, managed assembly, and startup scene, then chooses the active main camera. A project therefore needs a valid startup scene and at least one enabled Camera component for normal rendering.

### Export a distributable build

From the repository root:

```powershell
.\tools\Export-Game.ps1 `
  "C:\Projects\MyGame\MyGame.plutoproject" `
  "C:\Builds\MyGame\MyGame.exe"
```

The export helper:

1. Configures the `msvc-shipping` preset.
2. Builds a Release runtime without the editor or tests.
3. Builds the adjacent `*.Scripts.csproj`, if present.
4. Cooks project assets into a `.plutopack`.
5. Copies the runtime and native dependencies.
6. Bundles the required .NET runtime.

Distribute the entire generated folder, not just the `.exe`. The content pack reduces loose-file overhead and casual browsing, but its obfuscation is not cryptographic protection.

For more detail and the lower-level runtime command, see [Exporting a game](EXPORTING.md).

## Tests

Tests are enabled by CMake's standard `BUILD_TESTING` option and currently cover large-mesh optimization, model assets, and post-process initialization.

Build and run them from the MSVC tree:

```powershell
cmake --build out/build/msvc --config Debug
ctest --test-dir out/build/msvc -C Debug --output-on-failure
```

Run one group by name:

```powershell
ctest --test-dir out/build/msvc -C Debug `
  -R PlutoGEModelAssetTests `
  --output-on-failure
```

## CMake options and presets

| Option | Default | Description |
|---|---:|---|
| `PLUTO_BUILD_EDITOR` | `ON` | Build `PlutoGEEditor` |
| `PLUTO_BUILD_RUNTIME` | `ON` | Build `PlutoGERuntime` |
| `PLUTO_BUILD_SAMPLES` | `ON` | Reserved for sample applications; samples are not currently added by the root build |
| `BUILD_TESTING` | `ON` | Build and register the test executables |

| Preset | Purpose |
|---|---|
| `msvc` | Visual Studio 2022 x64 configure preset |
| `msvc-debug` | Debug build for the `msvc` tree |
| `msvc-shipping` | Release-only runtime configure preset with editor/tests disabled |
| `shipping` | Builds `PlutoGERuntime` in Release from `msvc-shipping` |
| `gcc` | Machine-specific w64devkit configure/build preset |
| `all` | Alias build preset using the `gcc` configure tree |

## Architecture

```text
PlutoGEEditor ──► EditorUI ───────┐
                                  │
PlutoGERuntime ───────────────────┼──► PlutoGE::engine
                                  │
                                  ├── Core
                                  ├── Platform
                                  ├── Render
                                  ├── Assets / Import
                                  ├── Scene
                                  ├── Audio
                                  └── Scripting ──► .NET 8 ScriptCore
```

The root `PlutoGE::engine` target is an interface target that collects the engine modules:

| Module | Responsibility |
|---|---|
| `Core` | Engine lifetime and coordination |
| `Platform` | Window, OpenGL context, input, and frame presentation |
| `Render` | Render graph/passes, cameras, materials, meshes, shaders, textures, and profiling |
| `Assets` | Projects, asset registry, asset references, loading, cooking, and packing |
| `Import` | Source-model import and mesh optimization |
| `Scene` | Entities, components, serialization, prefabs, physics, navigation, and baking |
| `Audio` | Device management, playback, spatialization, and listeners |
| `Scripting` | Managed build/reflection and the native/.NET bridge |
| `EditorUI` | Docking shell, panels, tools, selection, and editor workflow |

The scene owns the entity hierarchy and runtime systems. Components submit render work and participate in physics, audio, scripting, animation, navigation, and UI updates. The editor uses the same engine modules as the standalone runtime, while keeping editor-only panels and state in `EditorUI`.

## Repository layout

```text
PlutoGE/
├── editor/                 Editor executable, resources, and UI panels
├── engine/
│   ├── assets/             Projects, asset database, loading, and export
│   ├── audio/              Audio system
│   ├── core/               Engine facade and lifetime
│   ├── import/             Model and mesh import
│   ├── platform/           GLFW window and input
│   ├── render/             Renderer, passes, and post-processing
│   ├── scene/              ECS-like scene and native components
│   └── scripting/          Native host plus managed ScriptCore API
├── runtime/                Standalone player and export entry point
├── tests/                  CTest executables
├── third_party/            Vendored GLAD and FetchContent definitions
├── tools/                  Build/export helper scripts
├── docs/                   Detailed subsystem documentation
├── CMakeLists.txt          Root build
└── CMakePresets.json       Windows development and shipping presets
```

## Troubleshooting

### Configure cannot download a dependency

The first configure clones several dependencies. Confirm Git is on `PATH`, that the machine can access GitHub, and then rerun:

```powershell
cmake --preset msvc
```

### The editor fails to initialize rendering

Update the graphics driver and verify the GPU supports an OpenGL 4.3 core context. PlutoGE exits if it cannot prepare the required OpenGL context and function dispatch.

### Scripts do not appear in the component picker

- Install the .NET 8 SDK and ensure `dotnet` is on `PATH`.
- Put the source in the project's `Assets/Scripts` directory.
- Derive the class from `ScriptBehaviour`.
- Give it a parameterless constructor.
- Use **Runtime → Build Scripts** and fix errors shown in the console.

### `hostfxr.dll` cannot be found

Install a .NET runtime/SDK or set `DOTNET_ROOT` to the .NET installation. Exported games bundle a runtime in `DotnetRuntime`, while development builds also search `DOTNET_ROOT` and the standard Program Files installation.

### Export cannot find `PlutoGERuntime`

Build the runtime alongside the editor or use the shipping helper:

```powershell
cmake --build --preset shipping
```

### A standalone game opens without the expected scene

Open **Project Settings**, verify the startup scene, save the project, and ensure that scene contains an enabled camera. On Windows, runtime startup and scene diagnostics are written beside the executable to `PlutoGERuntime.log`.

## Further documentation

- [C# scripting specification](docs/CSHARP_SCRIPTING.md)
- [RmlUi project quick start](docs/RMLUI_QUICKSTART.md)
- [RmlUi integration notes](docs/RMLUI_INTEGRATION.md)
- [Networking architecture and roadmap](docs/NETWORKING.md)
- [Scripting subsystem overview](engine/scripting/README.md)
- [Exporting a game](EXPORTING.md)
- [Development task list](TODO.md)

## Contributing

PlutoGE is evolving quickly. Before submitting a change:

1. Keep module boundaries reflected in the existing CMake targets.
2. Add or update tests for behavior that can be exercised without the editor.
3. Run CTest with `--output-on-failure`.
4. Update the relevant documentation when changing project formats, scripting APIs, build commands, or export behavior.

## License

PlutoGE is licensed under the [Apache License 2.0](LICENSE). Third-party dependencies retain their own licenses.
