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
dependencies, bundled .NET runtime, and content pack. C# sources and source models classified as FBX/GLTF/GLB are excluded.
Other assets retain their existing representations, including imported binary meshes. Distribute the entire output folder, not only the executable.

Packs use an indexed version 2 format with independently compressed 256 KiB
blocks and CRC32 checks on the index and decoded blocks. The runtime mounts packs
and reads assets on demand instead of extracting the whole project at startup.
.NET assemblies and their runtime configuration files are materialized into a
private temporary directory because hostfxr requires filesystem paths. The
runtime removes that directory on normal exit. Version 1 packs remain readable;
older runtimes cannot read version 2 packs. Rebuild the runtime before exporting.
The editor skips incompatible runtimes, and export rejects an incompatible
selected executable before modifying the output folder.

Compression and CRCs are storage and corruption-detection features, not encryption
or publisher authentication. See [Content packs](docs/CONTENT_PACKS.md) for the
format, implementation plan, limits and patch workflow.

Default export still includes unreferenced assets to preserve dynamically loaded
content. Opt into dependency pruning and explicitly retain additional scenes or
script-selected assets:

```powershell
.\tools\Export-Game.ps1 "C:\Projects\MyGame\MyGame.plutoproject" "C:\Builds\MyGame\MyGame.exe" `
    -PruneUnused -AlwaysInclude 'project://Scenes/Level2.plutoscene','project://Items/Rare.plutoprefab'
```

The explicit roots include their transitive dependencies. Missing roots and
missing project dependencies stop pruned cooking. Managed assembly companions are
retained. `-NoCompression` writes raw blocks while keeping checksums and indexing.

The export command can also be run directly with an existing shipping runtime:

```powershell
.\PlutoGERuntime.exe --export <project.plutoproject> <output.exe>
```

Editor **Build Project** remains useful for development and script authoring because
it also exports the SDK. It reuses the existing engine runtime without rebuilding
it; rebuild the runtime as part of engine development when engine code changes. Use `Export-Game.ps1` for final player-facing builds.
