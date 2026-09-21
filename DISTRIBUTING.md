# Distributing the PlutoGE engine

This guide covers the 64-bit Windows distribution helper. The engine also has
a Linux build/install/TGZ workflow in the [README](README.md#arch-linux-1).
A release contains the editor, standalone
runtime, managed scripting SDK, editor resources, native runtime dependencies, and
documentation.

## Build a release

Prerequisites:

- Visual Studio 2022 or newer with Desktop development with C++
- CMake
- .NET 8 SDK
- Slang shader compiler (`slangc`) on `PATH` to generate RHI shader artifacts
- NSIS 3 when an `.exe` installer is required

From the repository root:

```powershell
.\tools\Build-EngineDistribution.ps1
```

Packages and `SHA256SUMS.txt` are written to `out/packages`. The ZIP is a portable
distribution. When NSIS is available, the same command also creates a Windows
installer.

The default helper build does not enable optional FSR 2 or Streamline/DLSS.
Configure those integrations separately when preparing an upscaler-enabled
distribution; see [FSR 2](docs/FSR2.md) and [DLSS](docs/DLSS.md).

## Validate on a clean Windows VM

1. Copy only the generated installer to the VM.
2. Install PlutoGE and launch it from the Start menu.
3. Create and save a new project outside the installation directory.
4. Add a C# script and use **Runtime > Build Scripts**.
5. Enter play mode and verify the startup scene.
6. Build the project to a new empty directory.
7. Run the exported game.
8. Uninstall PlutoGE and confirm the user project remains.

The editor requires the .NET 8 SDK for C# authoring. Exported games bundle the
installed .NET runtime and do not require end users to install .NET separately.

## Release requirements

PlutoGE is distributed under the Apache License 2.0. The engine license and
third-party notices are included in each package. Public releases should also be
Authenticode-signed and accompanied by SHA-256 checksums.
