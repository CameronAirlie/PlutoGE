# Headless game servers

## Editor workflow

Open **File > Project Settings** and scroll to **Dedicated Server**:

1. Select the server startup scene, independent of the graphical startup scene.
2. Set the output executable, for example `Build/Server/MyGameServer.exe`.
   Relative paths resolve from the project directory. Use a separate directory,
   outside Assets and Library. On Linux the editor removes the `.exe` extension.
3. Choose a tick rate (10 to 240 Hz). Add every simulation behaviour to the script
   allow list using the picker of behaviour classes from the project assembly.
4. Add application environment defaults for settings such as port, mode or bots.
   These values are stored in the project/package; configure secrets externally.
5. Save, then use **Build > Dedicated Server > Build** or **Build and Run**.
   **Run Dedicated Server** launches the existing package without rebuilding.

Build actions save the project, compile scripts, validate content, then reuse the
normal cooker/exporter with PlutoGEServer as the executable. A failed build prevents
launch. The host must be available: editor CMake builds also build PlutoGEServer
when PLUTO_BUILD_RUNTIME is enabled, and the Runtime installation includes it.

Copy the whole output directory to the server machine, including the matching pack,
native dependencies and managed-runtime files. Run the executable without arguments;
it finds its adjacent pack even from a different working directory. Ctrl+C stops it.
The editor starts an independent process; closing the editor does not stop it.
Do not start two servers on the same port.

CoD is configured with Main, 60 Hz, five simulation scripts and defaults for port 7777,
12 players, 8 bots and Hardpoint. Its output is `Build/DedicatedServer/CoDServer.exe`.
For Defusal, choose Foundry and set COD_SERVER_MODE to Defusal before building.

## Saved settings and deployment

Project manifests persist `SERVER_SCENE`, `SERVER_OUTPUT`, `SERVER_TICK_RATE`,
repeated `SERVER_SCRIPT`, and repeated `SERVER_ENV` name/value records. Older projects
keep working and must configure server settings before exporting. Export uses a copy
of the manifest with the simulation scene as the cooking root, preserving the source
graphical startup scene. Normal used-assets-only and always-include settings apply;
content loaded dynamically by scripts still needs explicit inclusion. Packages use
ordinary cooked content rather than stripped server-only assets.

`--scene`, repeated `--script` and `--tick-rate` replace saved defaults. A supplied
script list replaces the entire saved list. The inherited environment overrides
saved environment defaults; repeated `--env NAME=VALUE` overrides both, last wins.
Names must be portable identifiers, unique even ignoring case. PLUTOGE_HEADLESS is reserved.

```powershell
./CoDServer.exe --env COD_SERVER_PORT=7780 --env COD_SERVER_BOTS=4
./CoDServer.exe --scene project://Scenes/Foundry.plutoscene --env COD_SERVER_MODE=Defusal
```

Automation can call the same export API used by the editor:

```powershell
./PlutoGEServer.exe --export game.plutoproject Build/Server/MyGameServer.exe
```

Build managed scripts first for direct CLI exports. This command consumes saved
server/build settings and packages content; it does not run the editor's script
build or Project Validation UI. `--pack-version` reports host capability markers.

## Implementation plan

1. Persist independent server settings with backwards-compatible project defaults.
2. Reuse the cooker/exporter through ExportHeadlessProject; validate host capability
   and output location, then cook from the simulation scene using a manifest copy.
3. Discover adjacent packs in the host and apply saved defaults plus deployment overrides.
4. Wire editor settings and Build/Build-and-Run/Run actions; include the host in editor builds/installations.
5. Test persistence, overrides, pruning roots, unsafe output rejection, headless startup
   and CoD multiplayer against an exported host.

## Host internals

Build the PlutoGEServer CMake target with PLUTO_BUILD_RUNTIME enabled. This is a
console application using the same assets, managed runtime, physics and navigation
as a game, with EngineConfig.headless enabled. It never creates a GLFW window,
graphics device, swapchain or audio device. CPU mesh/skeleton data remains available
for collision and gameplay. Runtime UI, audio submission, render commands and render
physics extrapolation are skipped.

```powershell
./PlutoGEServer.exe --project game.plutoproject --scene project://Scenes/Arena.plutoscene --script Game.ServerSession --script Game.Bot --tick-rate 60
```

--project also accepts an exported .plutopack. RuntimeContent is shared with the
graphical runtime: it mounts the pack and explicit patch list, materializes managed
files and removes its temporary mount on exit. Include the platform native runtime
DLLs and .NET runtime needed by the normal game distribution.

--script is a repeated allow list of managed/native behaviour class names. Only
those classes can be instantiated in headless scenes, including dynamic prefabs.
List all simulation behaviours and exclude menus, cameras and local-player input.
Each listed class is validated against the loaded assembly before activation.
The list is intentionally explicit to avoid accidentally executing presentation or
local-input scripts when reusing authored game scenes.

The host uses a paced steady-clock fixed timestep (10–240 Hz, default 60), bounded
catch-up work, SIGINT/SIGTERM shutdown and optional --ticks N for automation.
A zero tick count runs continuously. Application.Quit requests stop this loop.
Scene transitions currently require restarting with a new --scene; a script request
fails explicitly instead of entering a graphical loading flow.

PLUTOGE_HEADLESS=1 is set only in the host process before loading managed scripts.
Games may use that process-local flag to configure dedicated session ownership;
engine code uses EngineConfig.headless as its source of truth. Do not use a hidden
game window as a substitute for headless initialization.

CoD also retains its optional launcher and packaging workflow in its Server directory
and Tools/BuildServer.ps1. Editor exports run the generic host directly and do not
need that launcher. The engine host contains no CoD-specific rules or networking assumptions.

PlutoGEHeadlessServerTests covers initialization without graphics, script allow
lists, collision, headless activation/shutdown, packed content and unsafe patch paths.
