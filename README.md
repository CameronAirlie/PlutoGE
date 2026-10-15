# PlutoGE

PlutoGE is a work-in-progress 3D game engine, editor, and standalone runtime written in C++20. It combines Vulkan and OpenGL rendering, an entity/component scene system, asset pipeline, physics, audio, runtime UI, and hosted .NET 8 C# scripting in one CMake-based repository.

The editor and runtime support Windows and Linux (including Arch Linux). It is under active development, so file formats and public engine APIs may change.

## Contents

- [Game developer manual](docs/README.md) — practical guides and examples for building games
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

- Controller menu navigation and keyboard/mouse versus controller prompts: [RmlUi controller guide](docs/RMLUI_AUTHORING.md#controller-menus-and-active-input-device)
- Project quality tier definitions and the editor Quality Settings pane: [graphics presets](docs/GraphicsPresets.md)
- Playable project templates, reusable gameplay kits, visual RML construction, automatic C# builds, and project editor commands: [authoring workflows](docs/EDITOR_AUTHORING.md)
- Dockable ImGui editor with editor and game viewports
- Scene hierarchy, inspector, content browser, console, and profiler
- Translate, rotate, and scale gizmos with local/world modes and snapping
- Perspective and orthographic editor cameras
- Entity selection, parenting, copy/paste, duplication, deletion, and undo/redo
- Multi-entity selection, group transforms, mixed-value editing, and prefab property variants
- Play-in-editor with configurable simulation speed
- Selectively keep serialized play-mode changes as one undoable edit
- Named viewport bookmarks with project-local persistence
- Content-browser asset reference lookup with background refresh and owner navigation
- Rotating scene recovery backups with per-project autosave settings
- Project validation with entity/asset navigation and export-blocking errors
- C# gameplay debug lines, wire spheres, and labels with category visibility controls
- Grouped undo/redo for serialized inspector and scene-setting edits
- Undoable ground placement with surface alignment and seeded yaw/scale variation
- Dedicated material, mesh, particle-system, shader-graph, animation-clip, and animation-graph editors
- Dockable Sequencer editor with track/key editing, binding repair, scrubbing, and restored visual previews
- Scene baking with fast, balanced, final, and custom quality settings
- Project settings for startup scene, script assembly, window size/title, VSync, editor camera, and post-processing

### Rendering

- Vulkan and OpenGL render hardware interface (RHI), Slang shaders compiled to SPIR-V/GLSL, and a legacy OpenGL deferred G-buffer pipeline
- Geometry, shadow, lighting, transparent, particle, ocean, physical-sky, volumetric-cloud, grid, runtime-UI, and post-process passes
- Directional, point, and spot lighting with shadow support
- Directional virtual shadow maps by default in the RHI, with explicit legacy cascaded shadows as an alternative
- Materials, textures, shader graphs, render targets, LODs, indirect drawing, frustum culling, and occlusion culling
- Environment maps, image-based lighting capture volumes, and baked probe volumes
- Terrain, foliage, splines, skeletal animation, particles, oceans, and volumetric clouds
- Post-processing including bloom, tone mapping, color grading, chromatic aberration, auto exposure, SSAO, SSGI, SSR, TAA, FXAA, motion blur, depth of field, lens flare, volumetric fog, gamma correction, and voxel cone tracing
- Voxel GI world caching and secondary bounce, rough reflections, glass, decals, toon shading, and outlines
- Procedural ocean waves, crest/shoreline foam, ripples, and shallow-water caustics
- Spatial upscaling, NVIDIA DLSS Super Resolution (Windows, used when the GPU supports it) and optional Vulkan FSR 2
- CPU/GPU render-pass timings and draw/submission statistics in the editor profiler

Standalone games disable OpenGL renderer profiling and Vulkan GPU timestamp/scope
profiling automatically. Editor hosts retain these diagnostics for the profiler.

Some advanced rendering paths are experimental and may depend on scene setup, compatible hardware, or generated bake data.
See [Rendering support](docs/RENDERING.md) for backend selection and limitations.
LPV/RSM effects remain in the legacy OpenGL pipeline; they are not supported RHI
post-process effects. VSM no longer maintains or automatically falls back to cascades.

### Gameplay systems

- Hierarchical entities with serializable native components
- Bullet-based rigidbodies, colliders, collision events, raycasts, and kinematic movement
- Skeletal/active ragdolls, cloth, skeleton attachments, and surface response assets
- OpenAL Soft audio, with XAudio2 integration on Windows
- 2D and 3D sound emitters/listeners, looping, one-shots, and audio obstruction support
- Navigation meshes and navigation agents
- Runtime canvas, image, text, and button components
- RmlUi document interfaces with RML/RCSS assets and managed controls
- Prefabs, tags, scene transitions, animation graphs/events, and scriptable data assets
- Camera rigs with follow/orbit, obstruction avoidance, shake, and target blending
- Additive scene sections with asynchronous reads, activation, cancellation, and unloading
- Sequencer playback for transforms, camera FOV, lights, audio, and script events
- Banked spline roads with collision, segment LODs, guardrails, and explicit junction/roadside bakes
- .NET 8 C# behaviours with editor-serialized fields and native component wrappers
- Fixed-step callbacks, gamepad input, and named input action maps
- Multi-client networking with reliable messaging, targeted sends, broadcasts, and JSON/binary payloads
- Entity replication with authority/ownership, full snapshots, late join, interpolation, and prefab scene bindings

## Technology

| Area | Implementation |
|---|---|
| Language | C++20 engine/editor; C#/.NET 8 gameplay API |
| Build | CMake 3.20+; presets require a compatible schema/generator version |
| Window/input | GLFW |
| Graphics | Vulkan (Volk/Vulkan Memory Allocator), OpenGL, Slang, GLAD, GLM |
| Editor UI | Dear ImGui docking branch and ImGuizmo |
| Game UI | Native canvas components and RmlUi/FreeType |
| Physics | Bullet |
| Audio | OpenAL Soft; XAudio2 on Windows |
| Networking | Managed .NET 8 TCP transport with main-thread event dispatch |
| Models | Assimp, tinygltf, meshoptimizer |
| Images | stb |
| Scripting host | `hostfxr` loading `PlutoGE.ScriptCore` |

Most third-party projects are acquired by CMake through `FetchContent`, so the first configure requires an internet connection.

## Requirements

### Arch Linux

Install the native build and runtime dependencies:

```bash
sudo pacman -S --needed base-devel cmake git tbb zenity \
  libx11 libxrandr libxinerama libxcursor libxi wayland \
  wayland-protocols libxkbcommon mesa
```

You need a GPU driver supporting the selected Vulkan or OpenGL backend and the .NET 8 SDK for C# authoring and gameplay scripting. Install `slangc` on `PATH` to compile RHI shaders. PlutoGE loads `libhostfxr.so` from `DOTNET_ROOT`, `/usr/share/dotnet`, `/usr/lib/dotnet`, or an exported game's bundled `DotnetRuntime` directory. `zenity` provides the editor's Linux file dialogs.

### Windows

The Windows workflow requires:

- A 64-bit Windows installation
- A GPU and driver supporting the selected Vulkan or OpenGL backend
- [Git](https://git-scm.com/)
- [CMake](https://cmake.org/) 3.20 or newer for the root build; use a version supporting the preset schema and selected Visual Studio generator
- Visual Studio with the **Desktop development with C++** workload matching the chosen preset's `generator`
- The Slang shader compiler (`slangc`) on `PATH` for RHI shader artifacts
- The .NET 8 SDK for building gameplay scripts
- PowerShell for the shipping export helper

Use the `msvc17` preset for Visual Studio 2022 or `msvc18` for Visual Studio 2026.
Vulkan headers and the loader integration are fetched by CMake; a machine-wide
Vulkan SDK is not required for the base renderer. Optional upscalers have
additional requirements in [FSR 2](docs/FSR2.md) and [DLSS](docs/DLSS.md).

## Build and run

### Arch Linux

```bash
git clone https://github.com/CameronAirlie/PlutoGE.git
cd PlutoGE
cmake --preset linux-debug
cmake --build --preset linux-debug -j"$(nproc)"
./out/build/linux-debug/editor/PlutoGEEditor
```

Use `linux-release` for an optimized build. The runtime is at `out/build/linux-debug/runtime/PlutoGERuntime`. To install into `out/install/linux-debug` or create a portable `.tar.gz` package:

```bash
cmake --install out/build/linux-debug
cpack --config out/build/linux-debug/CPackConfig.cmake
```

### Windows

Clone and configure the MSVC build for your Visual Studio version (`msvc17` for 2022, `msvc18` for 2026):

```powershell
git clone https://github.com/CameronAirlie/PlutoGE.git
cd PlutoGE
cmake --preset msvc18
cmake --build --preset msvc18-debug
```

Use `msvc18-release` (or `msvc17-release`) for an optimized build in the same tree.

The first configure downloads and configures the dependencies declared in [`third_party/CMakeLists.txt`](third_party/CMakeLists.txt), so it can take longer than later builds.

For day-to-day editor development, build only the editor and its dependencies:

```powershell
cmake --build --preset msvc18-debug --target PlutoGEEditor
```

Building `PlutoGEEditor` also builds `PlutoGERuntime` in the selected configuration
when `PLUTO_BUILD_RUNTIME` is enabled (the default), keeping the executable used
for game exports current. Existing exported games must be exported again to pick
up runtime changes. Building without `--target` additionally builds tests. Visual Studio
builds schedule individual compiles across all projects, one per core; configure
`-DPLUTO_MSVC_MAX_COMPILES=N` to cap them and reduce memory pressure.
The larger render, scene, asset, editor UI, and runtime targets use private
precompiled standard library, GLM, and glad headers. Configure with
`-DPLUTO_ENABLE_PCH=OFF` to compare build times or troubleshoot compiler
compatibility. Changing this option rebuilds those targets. ScriptCore builds
only when its tracked inputs change, and editor resources are copied
individually when changed, including resource-only edits.

Windows builds always include NVIDIA DLSS: the first configure downloads the
pinned Streamline SDK and stages its signed DLLs beside the executables. At
runtime DLSS is used only on supported NVIDIA GPUs and drivers; elsewhere it
stays disabled and rendering falls back to the built-in TAA. See
[DLSS](docs/DLSS.md). FSR 2 is off by default; enable it with
`-DPLUTO_ENABLE_FSR2=ON` (see [FSR 2](docs/FSR2.md)).

Run the editor from the `msvc18-debug` build:

```powershell
.\out\build\msvc18\editor\Debug\PlutoGEEditor.exe
```

The same build also produces the development runtime:

```powershell
.\out\build\msvc18\runtime\Debug\PlutoGERuntime.exe
```

If your generator places executables in a different subdirectory, find them with:

```powershell
Get-ChildItem .\out\build\msvc18 -Recurse -Filter PlutoGEEditor.exe
Get-ChildItem .\out\build\msvc18 -Recurse -Filter PlutoGERuntime.exe
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

The **Startup Scene** selector lists saved project scenes. Its selection is stored
in the project manifest; saving the project while editing another scene preserves
the selected startup scene.
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
| `Ctrl+Shift+C` | Copy profiler metrics (also available during play mode) |
| `Delete` | Delete the selected entity |

Editor camera navigation uses `W/A/S/D` for planar movement, `Q/E` for vertical movement, and Shift to move faster. Drag with the right mouse button to look and the middle mouse button to pan. On a trackpad, use `Alt` + left-drag to look and `Alt` + `Shift` + left-drag to pan. Two-finger scrolling adjusts movement speed (or zooms an orthographic view).

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
- Keyboard, mouse, gamepad, input action maps, cursor, and application state
- Raycasts, tagged raycasts, impulses/forces, and kinematic movement
- Prefab instantiation, deferred scene replacement, and additive scene sections
- Scriptable objects and safe project/user-data storage
- `OnCreate`, `OnUpdate`, `OnFixedUpdate`, `OnLateUpdate`, `OnDestroy`, collision, and animation-event callbacks
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

Start with the [game developer manual](docs/README.md) for editor setup and practical examples, then use the [C# scripting reference](docs/CSHARP_SCRIPTING.md) for supported serialized types and APIs. PlutoGE behaviours use `OnUpdate`, `OnFixedUpdate`, and `OnLateUpdate`; Unity APIs such as `MonoBehaviour`, `Transform`, and coroutines are not available.
See [PlutoGE networking](docs/NETWORKING.md) for the transport design, usage
model and wire format, and [entity replication](docs/ENTITY_REPLICATION.md) for
the implemented snapshot/session layer.

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
| `.plutosurface` | Footstep/impact surface responses and contact friction |
| `.rml` / `.rcss` | RmlUi documents and stylesheets |
| `.plutometa` | Asset metadata |
| `.plutopack` | Cooked standalone content container |

The model pipeline handles FBX and glTF/GLB source packages, generates discrete mesh/material/animation assets, and applies mesh optimization and LOD data. The asset system also recognizes OBJ meshes, common raster/HDR textures, WAV audio, and managed assemblies.

Generated mesh-import cache data is stored under `.plutoge-cache/` and is intentionally ignored by Git.

## Run and export a game

### Run a development project

Pass a project manifest directly to the runtime:

```powershell
.\out\build\msvc18\runtime\Debug\PlutoGERuntime.exe `
  "C:\Projects\MyGame\MyGame.plutoproject"
```

The runtime loads the manifest, managed assembly, and startup scene, then chooses the active main camera. A project therefore needs a valid startup scene and at least one enabled Camera component for normal rendering.

On Windows, double-clicking a built game opens no console window. Launching it
from CMD sends output to that console, and file/pipe redirection is supported.
CMD may return to its prompt while the game runs; use `start /wait "" Game.exe`
when you need it to wait for the game to exit.

### Export a distributable build

From the repository root:

```powershell
.\tools\Export-Game.ps1 `
  "C:\Projects\MyGame\MyGame.plutoproject" `
  "C:\Builds\MyGame\MyGame.exe"
```

The export helper reuses a prebuilt shipping runtime by default. On first export
or after engine changes, add `-RebuildRuntime`; use `-RuntimePath` to select a
different prebuilt runtime. With `-RebuildRuntime`, it:

1. Configures a Release runtime tree in `out/build/msvc-shipping`.
2. Builds a Release runtime without the editor or tests.
3. Builds the adjacent `*.Scripts.csproj`, if present.
4. Cooks project assets into a `.plutopack`.
5. Copies the runtime and native dependencies.
6. Bundles the required .NET runtime.

Distribute the entire generated folder, not just the `.exe`. The content pack reduces loose-file overhead and casual browsing, but its obfuscation is not cryptographic protection.

For more detail and the lower-level runtime command, see [Exporting a game](EXPORTING.md).

## Tests

Tests are enabled by CMake's standard `BUILD_TESTING` option. Coverage includes
assets/import/cooking, editor history and recovery, physics and camera rigs,
prefab variants, timelines, scene streaming, spline roads, managed input and
replication, and OpenGL/Vulkan rendering. Graphics suites require a working GPU
context; managed suites require `dotnet`. See `tests/CMakeLists.txt` for the
registered suites and build-dependent tests.

Build and run them from the MSVC tree:

```powershell
cmake --build out/build/msvc18 --config Debug
ctest --test-dir out/build/msvc18 -C Debug --output-on-failure
```

Run one group by name:

```powershell
ctest --test-dir out/build/msvc18 -C Debug `
  -R PlutoGEModelAssetTests `
  --output-on-failure
```

## CMake options and presets

| Option | Default | Description |
|---|---:|---|
| `PLUTO_BUILD_EDITOR` | `ON` | Build `PlutoGEEditor` |
| `PLUTO_BUILD_RUNTIME` | `ON` | Build `PlutoGERuntime` |
| `BUILD_TESTING` | `ON` | Build and register the test executables |
| `PLUTO_ENABLE_PCH` | `ON` | Precompile stable headers for larger targets |
| `PLUTO_ENABLE_FSR2` | `OFF` | Build the Vulkan FSR 2 integration |
| `PLUTO_ENABLE_STREAMLINE` | `ON` (Windows) | Build the Vulkan DLSS integration; downloads the Streamline SDK unless `PLUTO_STREAMLINE_SDK_ROOT` is set |

| Preset | Purpose |
|---|---|
| `linux-debug` / `linux-release` | Linux configure and build presets (Unix Makefiles) |
| `msvc17` | Windows x64 configure preset for Visual Studio 17 2022 |
| `msvc18` | Windows x64 configure preset for Visual Studio 18 2026 |
| `msvc17-debug` / `msvc17-release` | Debug/Release builds of the `msvc17` tree |
| `msvc18-debug` / `msvc18-release` | Debug/Release builds of the `msvc18` tree |

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
| `Platform` | GLFW windows, client API selection, input, and OpenGL context management |
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
└── CMakePresets.json       Linux and MSVC 17/18 debug/release presets
```

## Troubleshooting

### Configure cannot download a dependency

The first configure clones several dependencies. Confirm Git is on `PATH`, that the machine can access GitHub, and then rerun:

```powershell
cmake --preset msvc18
```

### The editor fails to initialize rendering

Check the project's Graphics API and update the matching GPU driver. OpenGL
requires a working core context; Vulkan requires a working loader/device and
compiled SPIR-V artifacts. Configure with `slangc` available and build the shader
target if CMake reported missing RHI shaders. See [Rendering support](docs/RENDERING.md).

### Scripts do not appear in the component picker

- Install the .NET 8 SDK and ensure `dotnet` is on `PATH`.
- Put the source in the project's `Assets/Scripts` directory.
- Derive the class from `ScriptBehaviour`.
- Give it a parameterless constructor.
- Use **Runtime → Build Scripts** and fix errors shown in the console.

### The `hostfxr` library cannot be found

Install a .NET runtime/SDK or set `DOTNET_ROOT` to the .NET installation. Exported games bundle a runtime in `DotnetRuntime`; development builds also search the standard Program Files installation on Windows and `/usr/share/dotnet` or `/usr/lib/dotnet` on Linux.

### Export cannot find `PlutoGERuntime`

Build the runtime alongside the editor or let the export helper build it:

```powershell
.\tools\Export-Game.ps1 <project> <output.exe> -RebuildRuntime
```

### A standalone game opens without the expected scene

Open **Project Settings**, verify the startup scene, save the project, and ensure that scene contains an enabled camera. On Windows, runtime startup and scene diagnostics are written beside the executable to `PlutoGERuntime.log`.

## Further documentation

- [Game developer manual and feature index](docs/README.md)
- [Build your first playable game](docs/guide/first-game.md)
- [Feature milestones](docs/FEATURE_MILESTONES.md)
- [Editor iteration tools](docs/EDITOR_WORKFLOWS.md)
- [Foliage, spatial cells, and tree collision](docs/FOLIAGE.md)
- [C# scripting specification](docs/CSHARP_SCRIPTING.md)
- [RmlUi project quick start](docs/RMLUI_QUICKSTART.md)
- [RmlUi integration notes](docs/RMLUI_INTEGRATION.md)
- [Networking architecture and roadmap](docs/NETWORKING.md)
- [Skeletal and active ragdolls](docs/RAGDOLLS.md)
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
