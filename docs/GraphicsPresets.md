# Runtime graphics presets

## Architecture and implementation plan

1. Separate engine quality policy from authored scenes and display controls.
2. Define Low, Medium, High and Ultra centrally in `render::GraphicsQuality`.
3. Apply shadow ceilings before draw preparation and filter expensive effects before resource and GI requests.
4. Propagate settings to main cameras, overlays and render textures; reset temporal history on changes.
5. Expose validated scripting lookup, custom settings, readback and reset. Store player preferences in the game.

Engine defaults retain scene-authored quality. Presets survive scene loads without editing assets or enabling effects absent from the scene. Resolution, VSync and temporal upscaling remain independent.

| Tier | Cascaded directional shadow ceiling | Distance ceiling | Skipped effects |
| --- | --- | --- | --- |
| Low | 512, 1 cascade; selects cascaded shadows | 40 | SSAO, GI, SSR, volumetric fog/clouds, bloom/lens flare, DOF, motion blur |
| Medium | 1024, 2 cascades; selects cascaded shadows | 80 | GI, SSR, volumetric fog/clouds, DOF, motion blur |
| High | 2048, 4 cascades; authored method | 150 | DOF, motion blur |
| Ultra | 4096, 4 cascades; authored method | 300 | None |

High/Ultra retain authored virtual-shadow budgets. Local shadows, texture residency, particles and mesh LOD are unchanged. Tone mapping, grading, exposure, sky and antialiasing are preserved. Low tiers trade indirect lighting and atmospheric fidelity for reduced GPU work; representative low-end hardware still needs benchmarking.

## Scripting

```csharp
GraphicsSettings.TryApplyPreset(GraphicsPreset.Low);
if (GraphicsSettings.TryGetPreset(GraphicsPreset.Medium, out var quality))
    GraphicsSettings.TryApplyQuality(quality with {
        ShadowResolution = 512, ShadowDistance = 60, Bloom = false
    });
GraphicsSettings.TryGetQuality(out var current);
GraphicsSettings.TryResetQuality(); // Restore authored quality
```

`QualitySupported` checks bridge availability. Unsupported calls return false; invalid enums, non-finite distances and out-of-range values throw argument exceptions before crossing the bridge. Native setters also validate. Call on the script/main thread. The 16-byte ABI uses fixed-width integers, a float and explicit flags; presets originate in native code.

Native hosts use `RhiRenderService::SetGraphicsQuality` with customised `GraphicsQuality::FromPreset` values. Direct scene renderers can opt in with `SetGraphicsQuality`; the editor scene view retains authored defaults, while the game preview follows runtime quality during Play and restores authored quality on stop.

## CoD

`GameGraphicsSettings` binds a selector to the title and pause menus. It cycles Low, Medium, High, Ultra and saves successful selections to `%LOCALAPPDATA%/PlutoCombat/graphics.json`. First launch defaults to High. Invalid/unreadable preferences fall back to High. Atomic file replacement keeps saved preferences intact if a write fails; the active selection still applies and a warning is logged. Opening either menu applies saved settings, including direct gameplay launch.

## Tests

`PlutoGEGraphicsQualityTests` checks tier validity, ceilings, authored settings preservation, effect gates, custom overrides and reset. `PlutoGEGraphicsQualitySmokeTests` checks unsupported registration, wire layout, flag roundtrips, managed validation and reset. Build ScriptCore and the native runtime together because the scripting bridge adds a required registration export.
