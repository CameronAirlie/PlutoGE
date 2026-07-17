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

On its first run it configures and builds `PlutoGEEditorHost`, installs the UI
dependencies, and starts Electron. Later runs can skip work when appropriate:

```powershell
./tools/Start-ElectronEditor.ps1 -SkipNativeBuild -SkipInstall
```

The host/editor boundary is deliberately narrow: Electron sends viewport
bounds and visibility commands over standard input, while the host returns
newline-delimited JSON lifecycle events. Keep engine and scene APIs in the
native host rather than exposing C++ objects directly to the renderer process.

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
