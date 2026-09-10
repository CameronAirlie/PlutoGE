# Exporting a game

Use the shipping export script from the repository root:

```powershell
.\tools\Export-Game.ps1 "C:\Projects\MyGame\MyGame.plutoproject" "C:\Builds\MyGame\MyGame.exe"
```

Exports reuse the prebuilt Release runtime at
`out/build/msvc-shipping/runtime/Release/PlutoGERuntime.exe`. Build it once, and
rebuild it after changing engine code or runtime configuration, by adding
`-RebuildRuntime` to the command above. This explicitly configures and builds the
shipping runtime without the editor, samples, or tests. To use a runtime from an
engine distribution or another build, pass `-RuntimePath "C:\PlutoGE\bin\PlutoGERuntime.exe"`
instead. Keep its accompanying dependencies in place. A missing runtime stops the
export with instructions; normal exports never invoke CMake.

The script builds an adjacent game script project when present, then cooks the project's assets into one `<GameName>.plutopack` container
and creates a self-contained game folder containing the executable, native
dependencies, bundled .NET runtime, and content pack. Loose source assets and C#
sources are not shipped. Distribute the entire output folder, not only the executable.

The content pack reduces file-system overhead and prevents casual browsing. Its byte
obfuscation is not cryptographic copy protection; determined users can still inspect
content loaded on their own machine.

The export command can also be run directly with an existing shipping runtime:

```powershell
.\PlutoGERuntime.exe --export <project.plutoproject> <output.exe>
```

Editor **Build Project** remains useful for development and script authoring because
it also exports the SDK. It reuses the existing engine runtime without rebuilding
it; rebuild the runtime as part of engine development when engine code changes. Use `Export-Game.ps1` for final player-facing builds.
