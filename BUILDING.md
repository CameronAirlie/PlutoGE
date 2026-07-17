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
