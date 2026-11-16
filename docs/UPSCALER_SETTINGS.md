# Upscaler settings in games

A project chooses its temporal upscaler in **Project Settings > Runtime Upscaler** ([DLSS.md](DLSS.md),
[FSR2.md](FSR2.md)). A game can let the player choose instead, from a settings menu, through `GraphicsSettings`:

```csharp
// Offer only what this machine can run (None, native resolution with TAA, always can).
var dlss = GraphicsSettings.GetUpscalerSupport(TemporalUpscaler.Dlss);
if (!dlss.Supported)
    Debug.Log($"DLSS unavailable: {dlss.Reason}");

GraphicsSettings.TrySetUpscaler(new UpscalerSettings(TemporalUpscaler.Fsr2, UpscalerQuality.Balanced, Sharpness: 0.3f));
var current = GraphicsSettings.Upscaler;           // what frames render with now
bool chosen = GraphicsSettings.IsUpscalerOverridden; // a script's choice, not the project's
GraphicsSettings.TryResetUpscaler();                // back to the project's upscaler
```

- **What a game chooses:** the technology (`None`, `Dlss`, `Fsr2`), the quality mode, and the sharpening applied after
  reconstruction (0-1; it also sharpens native TAA). `UpscalerQuality.NativeAntiAliasing` renders at full resolution
  and only anti-aliases: DLAA with DLSS, native AA with FSR 2. The rest of the renderer's upscaler setup (HDR input,
  exposure) stays as the host configured it.
- **Support:** DLSS needs Vulkan, a supported NVIDIA GPU and driver and the Streamline modules; FSR 2 needs Vulkan. A
  headless server has no render device, so only `None` is supported there. `Reason` is a sentence suitable for a menu.
  An unsupported choice is still accepted: the renderer falls back to native TAA (as it does for a project setting),
  so a settings file copied from another machine never breaks the game.
- **Lifetime:** the choice lasts until the game stops. In the editor, the Game view follows it during Play and returns
  to the project's upscaler when Play stops; the Editor view always keeps the project's. Games apply their saved
  settings at start (SurvivalGame's `PlayerSettings` does).
- **Validation:** `UpscalerSettings.IsValid` is checked before anything crosses the bridge; invalid enums and a
  non-finite or out-of-range sharpness throw `ArgumentOutOfRangeException`. Calls return false when the host has no
  bridge (`UpscalerSettingsSupported`).

## Engine side

`Engine::SetRuntimeTemporalUpscaler(std::optional<rhi::TemporalUpscalerChoice>)` holds the game's choice over
`EngineConfig::temporalUpscaler`. `Engine::GetTemporalUpscalerOptions` is what the engine's renderer uses, and
`Engine::StopRuntime` clears the choice. `rhi::TemporalUpscalerChoice` is the part a player chooses; `ApplyTo` lays it
over a host's options, which is how the editor's Game view applies it to its own renderer.
`Engine::GetTemporalUpscalerSupport` asks the render device.

The managed bridge is one entry point, `ControlUpscaler` (`engine/scripting/src/UpscalerScriptApi.{h,cpp}`,
`ScriptBridge.Upscaler.cs`), taking a 284-byte request whose size both sides check. Tests:
`PlutoGETemporalUpscalerChoiceTests` (native) and `PlutoGEUpscalerSmokeTests` (the managed contract).
