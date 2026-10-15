# Runtime graphics presets

## Architecture and implementation plan

1. Separate engine quality policy from authored scenes and display controls.
2. Define engine defaults in `render::GraphicsQuality`; let projects override the four definitions through `GraphicsQualityPresets`.
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

`QualitySupported` checks bridge availability. Unsupported calls return false; invalid enums, non-finite distances and out-of-range values throw argument exceptions before crossing the bridge. Native setters also validate. Call on the script/main thread. The 16-byte ABI uses fixed-width integers, a float and explicit flags; presets originate in the native project definitions, falling back to engine defaults.

Native hosts install project definitions with
`RhiRenderService::SetGraphicsQualityPresets`, then pass
`GetGraphicsPreset(preset)` to `SetGraphicsQuality` to apply a project tier.
`GraphicsQuality::FromPreset` always returns the built-in defaults, including for
the editor reset button. Direct scene renderers can opt in with
`SetGraphicsQuality`.


## Editor

Open **View > Quality Settings** to edit each project's Low, Medium, High and
Ultra definitions. Controls expose every supported quality policy field:

| Control | Meaning |
|---|---|
| Apply tier overrides | Off uses scene settings with no tier override |
| Resolution ceiling | 256–8192; caps cascaded directional shadow-map resolution |
| Cascade ceiling | 1–4; caps authored cascade count |
| Distance ceiling | 1–10000 world units; caps directional shadow distance and positive caster-distance limits |
| Use cascaded shadows | On selects cascaded directional shadows; off preserves the authored method |
| Ambient occlusion | Allow SSAO |
| Global illumination | Allow SSGI and VCT GI |
| Screen-space reflections | Allow SSR |
| Volumetrics | Allow volumetric fog and clouds |
| Bloom and lens flare | Allow both effects |
| Depth of field / Motion blur | Allow the corresponding effect |

An allowed effect still requires scene authoring to enable it. Effects retain
their scene-authored strength, resolution and sample settings; this pane edits the
existing runtime policy, not arbitrary per-effect parameters. Virtual-shadow
budgets, local shadows, material environment reflections, display settings,
texture residency, particles and mesh LOD remain independent.

Edits mark the project dirty. Save with **File > Save Project** (Ctrl+S).
**Reset this tier to engine defaults** restores only that tier.
**Preview tier in Scene viewport** applies it to the Scene viewport.
Both viewport preset selectors and managed preset lookup use project definitions.
An already applied policy is a snapshot: reapply a tier after editing it.
Editing definitions is disabled during Play.

Project manifests serialize four `QUALITY_PRESET` records, indexed 0–3, with
enabled, shadow resolution, cascade count, distance and the eight policy flags
(cascaded method, AO, GI, reflections, volumetrics, bloom, DOF, motion blur).
Missing records retain engine defaults. Invalid records reject loading with an
error; invalid in-memory tiers reject saving before opening the manifest for
writing. Exports copy the definitions into the runtime manifest. Editor project
switches and standalone startup install the definitions before scripts run.
Player-selected tier persistence remains owned by the game.

Open a viewport's **Quality** popup and select **Graphics preset**: Scene settings, Low, Medium, High or Ultra. Scene settings restores authored quality; a customised runtime policy displays Custom. This control requires the RHI preview.

Scene and Game viewports retain independent preview selections for the editor session. During Play, the Game viewport follows runtime quality, and changing its preset updates the runtime policy immediately, including what scripts read. Stopping Play restores its previous preview selection. The Scene viewport keeps its own selection throughout. Preview choices do not modify scene assets or the game's saved preferences.

## CoD

`GameGraphicsSettings` binds a selector to the title and pause menus. It cycles Low, Medium, High, Ultra and saves successful selections to `%LOCALAPPDATA%/PlutoCombat/settings.json`. The game migrates legacy `graphics.json` preferences. First launch defaults to High. Invalid/unreadable preferences fall back to High. Atomic file replacement keeps saved preferences intact if a write fails; the active selection still applies and a warning is logged. Opening either menu applies saved settings, including direct gameplay launch.

## Tests

`PlutoGEGraphicsQualityProjectTests` checks legacy defaults, every tier field and float roundtrips, runtime manifest preservation, runtime lookup, atomic table validation, invalid-save preservation and malformed records. `PlutoGEGraphicsQualityTests` checks tier validity, ceilings, authored settings preservation, effect gates, custom overrides and reset. `PlutoGEGraphicsQualitySmokeTests` checks unsupported registration, wire layout, flag roundtrips, managed validation and reset. Build ScriptCore and the native runtime together because the scripting bridge adds a required registration export.
