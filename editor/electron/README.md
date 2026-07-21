# PlutoGE Electron editor distribution

Electron Forge is the primary PlutoGE editor distribution pipeline on Windows. The distribution contains the Electron UI, native viewport host, shipping game runtime, ScriptCore, and a private .NET 8 SDK/runtime used to compile project scripts and export self-contained games.

From the repository root, build the self-contained ZIP distribution with:

```powershell
./tools/Package-ElectronEditor.ps1
```

For an unpacked application directory without installers:

```powershell
./tools/Package-ElectronEditor.ps1 -ForgeTarget package
```

During iteration, existing native and npm outputs can be reused. If the Electron binary payload is missing, the script automatically repairs the npm installation even when `-SkipNpmInstall` is supplied:

```powershell
./tools/Package-ElectronEditor.ps1 -SkipNativeBuild -SkipNpmInstall
```

Artifacts are written below `editor/electron/out`. Every packaging run invokes `tools/Verify-ElectronPackage.ps1`, which can also be run independently. The script creates a reusable Electron runtime ZIP from `node_modules/electron/dist`, so Forge does not perform an additional Electron download. The legacy Squirrel/NuGet maker is intentionally not used because it cannot reliably stream the bundled .NET SDK payload.

Requirements are Visual Studio with the C++ workload, CMake, Node.js/npm, a .NET 8 runtime/reference pack, and a .NET 8 or newer SDK capable of targeting `net8.0`. Distribution builds use Release native binaries and a private .NET installation; installed users do not need CMake or a system .NET installation to edit, compile scripts, or export projects.

Electron 43 requires Node.js 22.12 or newer. If `node_modules/electron/dist` is incomplete, the packaging script first reuses its staged archive or Electron's local download cache, then falls back to `npm rebuild electron`. On networks with TLS inspection, configure npm's trusted CA or `NODE_EXTRA_CA_CERTS`; do not disable certificate verification.
