# Exporting a game

Use the shipping export script from the repository root:

```powershell
.\tools\Export-Game.ps1 "C:\Projects\MyGame\MyGame.plutoproject" "C:\Builds\MyGame\MyGame.exe"
```

The script configures and builds a Release runtime without the editor, samples, or
tests. It then cooks the project's assets and creates a self-contained game folder
containing the named executable, project manifest, native dependencies, and bundled
.NET runtime. Distribute the entire output folder, not only the executable.

The export command can also be run directly with an existing shipping runtime:

```powershell
.\PlutoGERuntime.exe --export <project.plutoproject> <output.exe>
```

Editor **Build Project** remains useful for development and script authoring because
it also exports the SDK. Use `Export-Game.ps1` for final player-facing builds.
