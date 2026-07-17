# Building PlutoGE

The default build keeps the engine split into its existing static module
libraries. To build all engine modules into one shared library instead, enable
`PLUTO_BUILD_ENGINE_SHARED`:

```powershell
cmake -S . -B out/build/shared-engine -DPLUTO_BUILD_ENGINE_SHARED=ON
cmake --build out/build/shared-engine --config Debug
```

On Windows this produces `PlutoGE.dll` and its `PlutoGE.lib` import library.
Executables and runtime DLLs are placed together under the build directory's
`bin` folder so the editor and runtime can be launched directly.

With the repository's Visual Studio preset, the equivalent commands are:

```powershell
cmake --preset msvc-shared-engine
cmake --build --preset msvc-shared-engine-debug
```

Consumers should link the umbrella target:

```cmake
target_link_libraries(MyEditorProcess PRIVATE PlutoGE::engine)
```

The existing module target names such as `PlutoGE::Core`, `PlutoGE::Render`,
and `PlutoGE::Scene` remain valid in both build modes.

## Electron editor

The Windows editor uses Electron for its interface and a small native process
for the engine. The native process links `PlutoGE.dll` and aligns an owned,
borderless GLFW window over the Electron viewport, so frames remain on the GPU
instead of being copied through JavaScript. On Windows, an owned overlay is
required because Chromium's DirectComposition surface covers cross-process
child windows even when their Win32 child z-order is correct.

For the normal edit/build/run loop, use the repository script:

```powershell
./tools/Start-ElectronEditor.ps1
```

When the repository is open in VS Code, no terminal command is required. Pick
**PlutoGE: Electron Editor** in the Run and Debug view and press `F5`, or press
`Ctrl+Shift+B` to run the default build task. Both actions call the same script
above, including configuration, native build, dependency installation, and
launch. After the first successful build, use **PlutoGE: Electron Editor
(Fast)** from Run and Debug or **Tasks: Run Task** to skip the native build and
dependency check.

On its first run it configures and builds `PlutoGEEditorHost`, installs the UI
dependencies, and starts Electron. Later runs can skip work when appropriate:

```powershell
./tools/Start-ElectronEditor.ps1 -SkipNativeBuild -SkipInstall
```

The host/editor boundary is deliberately narrow: Electron sends viewport
bounds and visibility commands over standard input, while the host returns
newline-delimited JSON lifecycle events. Keep engine and scene APIs in the
native host rather than exposing C++ objects directly to the renderer process.

The Electron editor currently exposes native scene hierarchy and selection,
entity transforms and activation, serialized component properties, component
creation/removal, reparenting, scene open/save, undo/redo, and play/stop. The
engine remains the source of truth; React renders snapshots and submits typed
commands through the secure preload bridge.

The edit viewport uses the same fly-camera controls as the ImGui editor: hold
the right mouse button and use WASD/QE, hold Shift to boost, and use the wheel
to adjust movement speed. Use the Camera button to edit its transform,
projection, movement speed, grid visibility, or frame the selected entity.
Play mode renders through the scene's main camera.

The Camera inspector also exposes the editor camera's ordered post-processing
stack. Effects can be added from the engine registry, enabled or disabled,
moved up or down, removed, and edited through their typed parameters. Preset
references can be loaded, cleared, or saved. Selecting an entity with a Camera
component exposes the same stack controls for that scene camera. Editor-camera
settings are persisted by Save Project; scene-camera settings are persisted by
Save Scene.

Use Project... in the Electron toolbar to open a `.plutoproject`. This applies
the same project asset context, startup scene, VSync setting, and saved editor
camera settings used by the ImGui editor. Opening only a scene without its
project can leave `project://` mesh and material references unresolved. Use
Save Project to persist the Editor Camera transform, projection, and base move
speed back into the project manifest.

To run the UI manually, point it at a built host before starting Forge:

```powershell
$env:PLUTOGE_ENGINE_HOST = (Resolve-Path 'out/build/electron-editor/bin/Debug/PlutoGEEditorHost.exe')
Set-Location editor/electron
npm start
```

For a packaged editor, set `PLUTOGE_ENGINE_BUNDLE_DIR` to a directory named
`engine` containing `PlutoGEEditorHost.exe`, `PlutoGE.dll`, and their runtime
DLL dependencies before running `npm run make`. Forge copies that directory
into the application's resources.
