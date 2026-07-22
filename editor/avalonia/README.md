# PlutoGE Avalonia editor

This editor hosts PlutoGE in the Avalonia process. Avalonia owns each OpenGL
context and compositor surface; `PlutoGE.Editor.Native` receives the current
framebuffer and GL procedure resolver only during `OpenGlControlBase` lifecycle
callbacks. The native boundary uses opaque engine and viewport handles plus
fixed-layout scene, transform, input, and timing records.

## UI architecture

The editor follows MVVM around one shared session:

- `EditorShellWindow` owns a four-region dock workspace. Project, hierarchy,
  viewport, and inspector panes can be tabbed, reordered by dragging their tab
  handles, moved between left/center/right/bottom regions, resized with the
  splitters, hidden and restored from View, or floated into top-level windows
  and docked back without losing pane state.
- `EditorSessionViewModel` owns the loaded project, scene assets, hierarchy,
  selection, and component inspector state shared by those windows.
- `ProjectPaneView` opens `.plutoproject` manifests and `.plutoscene` files. It
  lists the refreshed project asset registry and can load any registered scene.
- `InspectorPaneView` retains editable transforms and lists every native component
  plus the values exposed by that component's serialization contract.
- `EngineHost` is the in-process native service/model. C++ handles never leak
  into the view. The bridge owns project and scene loading and exposes component
  metadata through fixed-layout records.
- `EngineViewport` is the small view-specific boundary that owns OpenGL and raw
  pointer/keyboard input. The docked viewport keeps its `ViewportViewModel`
  while moving between regions or floating, and hardware-validation windows
  still use independent viewport state.

Reusable editor controls live in `Controls`, with their shared visual language
in `Themes/EditorControls.axaml`. `EditorPanel`, `PropertySection`,
`EditorToolButton`, `StatusPill`, `Vector3Input`, and `LabeledNumberInput` are
intended as the base vocabulary for new panels. Palette, focus, hover, pressed,
density, and numeric input styling are centralized rather than copied into
individual views.

Viewport input is captured at the handled routed-event boundary so Avalonia's
composition visual cannot consume it before the editor sees it. Right-drag
controls mouse look; keep the right button held while using WASD/QE to fly, and
hold Shift to boost. A left click performs native mesh/terrain bounds picking
through the opaque viewport handle. ImGui and ImGuizmo hover/active state blocks
that pick so overlay buttons and transform handles retain ownership of the
pointer.

## Build and run

From the repository root, one command configures when necessary, incrementally
rebuilds the C++ engine bridge and Avalonia UI, stages the native dependencies,
and runs the editor:

```powershell
.\run-editor.cmd
```

Useful variants:

```powershell
.\run-editor.cmd -Configuration Release
.\run-editor.cmd -BuildOnly
.\run-editor.cmd -Reconfigure
.\run-editor.cmd -ViewportValidation
```

Close a running editor before rebuilding because Windows keeps the in-process
native DLL locked. The script uses `out/build/editor` as its stable CMake build
tree and reports that situation directly rather than failing during linking.

PlutoGE currently requires desktop OpenGL 4.3. Windows is therefore configured
for Avalonia's WGL renderer rather than its default ANGLE renderer. X11 requests
GLX 4.3. macOS cannot run the current renderer until PlutoGE gains an OpenGL 4.1
or another graphics backend, even though the surrounding Avalonia UI is portable.

## Validation

The deterministic validation checks continuous camera rotation, the resize
sequence, synthetic 60/120/144 Hz cadence, and independent per-window state:

```powershell
dotnet run --project editor/avalonia/tests/PlutoGE.Editor.Avalonia.Validation.csproj -c Debug
```

The hardware/compositor run opens one editor window per detected monitor,
continuously rotates every camera, and cycles window sizes while showing frame
and resize timings:

```powershell
dotnet run --project editor/avalonia/PlutoGE.Editor.Avalonia.csproj -c Debug -- --viewport-validation
```

Use that mode on the target 60, 120, and 144 Hz displays. Check for smooth
rotation, stable average frame time near 16.67, 8.33, and 6.94 ms respectively,
correct rendering after every resize, and no `context not shared` status in
multi-window operation.

## Native-child fallback

Do not replace the Avalonia shell based on frame cadence alone. Capture
end-to-end input-to-photon latency in the hardware validation run. If the
composited viewport adds unacceptable latency, keep this C API and the Avalonia
hierarchy/inspector, then replace only `EngineViewport` with a platform
`NativeControlHost` child whose swapchain is presented directly by PlutoGE.
That fallback is intentionally isolated to the viewport control.
