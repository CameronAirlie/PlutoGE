# Runtime graphics presets

## Architecture and implementation plan

1. Separate engine quality policy from authored scenes and display controls.
2. Define engine defaults in `render::GraphicsQuality`; let projects override the four definitions through `GraphicsQualityPresets`.
3. Apply shadow ceilings before draw preparation and filter expensive effects before resource and GI requests.
4. Propagate settings to main cameras, overlays and render textures; reset temporal history on changes.
5. Expose validated scripting lookup, custom settings, readback and reset. Store player preferences in the game.

Engine defaults retain scene-authored quality. Presets survive scene loads without editing assets or enabling effects absent from the scene. Resolution, VSync and temporal upscaling remain independent.

| Tier | Cascaded directional shadow ceiling | Distance ceiling | Skipped effects | Foliage detail | Grass density / distance |
| --- | --- | --- | --- | --- | --- |
| Low | 512, 1 cascade; selects cascaded shadows | 40 | SSAO, GI, SSR, volumetric fog/clouds, bloom/lens flare, DOF, motion blur, contact shadows, traced shadows, foliage shadow sway, terrain anti-tiling | 60% | 40% / 50% |
| Medium | 1024, 2 cascades; selects cascaded shadows | 80 | GI, SSR, volumetric fog/clouds, DOF, motion blur | 75% | 70% / 75% |
| High | 2048, 4 cascades; authored method | 150 | DOF, motion blur | 100% | 100% / 100% |
| Ultra | 4096, 4 cascades; authored method | 300 | None | 100% | 100% / 100% |

Foliage detail caps `BasicLighting::foliageDetail` ([FOLIAGE.md](FOLIAGE.md)). Grass ceilings scale each detail grass
layer's authored density, and its draw, full-density and LOD distances together ([DETAIL_GRASS.md](DETAIL_GRASS.md)).
Contact shadows are skipped for the sun and every local light.

**Foliage shadow sway** (off on Low): swaying foliage holds its rest pose in the sun's virtual shadow maps, so their
pages stay cached. Under a dense canopy, sway within the wind's Shadow Sway Distance redraws most visible pages every
frame: in SurvivalGame's LakeDistrict forest (Vega 8, 853x480) it kept about 216 pages dirty and VSM page rendering at
5.2 ms; frozen, 4 pages and 0.25 ms. Lowering Shadow Sway Distance alone did not help there, because the few pages left
dirty still held the whole per-frame triangle budget. The plants themselves still sway.

**Terrain anti-tiling** (off on Low): terrain layers tile plainly instead of stochastically, so each layer map is
fetched once rather than twice (1.3-1.45 ms of a 24 ms frame in the LakeDistrict forest on a Vega 8; repeats show on
open ground). It is applied per frame (`BasicLighting::terrainAntiTiling`), so switching tiers takes effect at once.

**Water reflections** follow the reflections flag: on tiers without screen-space reflections, water traces
`GraphicsQuality::WaterReflectionStepsWithoutReflections` (16) steps per pixel instead of 48
(`BasicLighting::waterReflectionSteps`), so it keeps reflecting the scene, more coarsely.

High/Ultra retain authored virtual-shadow budgets. Local shadows, texture residency, particles and mesh LOD are unchanged. Tone mapping, grading, exposure, sky and antialiasing are preserved. Low tiers trade indirect lighting and atmospheric fidelity for reduced GPU work; representative low-end hardware still needs benchmarking.

Baked ambient volumes and reflection captures are not effects, so every tier keeps them, including their live sky,
sky reflection occlusion and capture relighting. Low therefore still has ambient light, reflections and occlusion
([BAKED_ENVIRONMENT_LIGHTING.md](BAKED_ENVIRONMENT_LIGHTING.md)).

## Scripting

```csharp
GraphicsSettings.TryApplyPreset(GraphicsPreset.Low);
if (GraphicsSettings.TryGetPreset(GraphicsPreset.Medium, out var quality))
    GraphicsSettings.TryApplyQuality(quality with {
        ShadowResolution = 512, ShadowDistance = 60, Bloom = false, GrassDensity = 0.5f
    });
GraphicsSettings.TryGetQuality(out var current);
GraphicsSettings.TryResetQuality(); // Restore authored quality
```

`QualitySupported` checks bridge availability. Unsupported calls return false; invalid enums, non-finite distances and out-of-range values throw argument exceptions before crossing the bridge. Native setters also validate. Call on the script/main thread. The 28-byte ABI (`GraphicsQualityNative`) holds two fixed-width integers, the shadow distance, thirteen explicit flag bits (bit 9 is contact shadows, bit 10 traced shadows, bit 11 foliage shadow sway, bit 12
terrain anti-tiling) and the foliage detail, grass density and grass distance floats; presets originate in the native project definitions, falling back to engine defaults. `GraphicsQuality` exposes the accepted ranges as constants (`MinGrassDensity`, `MaxShadowDistance`, ...) for settings menus.

Native hosts install project definitions with
`RhiRenderService::SetGraphicsQualityPresets`, then pass
`GetGraphicsPreset(preset)` to `SetGraphicsQuality` to apply a project tier.
`GraphicsQuality::FromPreset` always returns the built-in defaults, including for
the editor reset button. Direct scene renderers can opt in with
`SetGraphicsQuality`.


## Editor

Open **Window > Tools > Quality Settings** to edit the project's Low, Medium, High and Ultra definitions side by side.
Each row is one ceiling and each column one tier:

- **Apply tier** switches a tier's overrides on or off; an off tier leaves scene settings untouched and its cells are
  disabled.
- Rows are grouped (Shadows, Lighting, Post-processing, Vegetation). Hover a setting for what it controls, or a cell for
  the tier's engine default.
- Cells that differ from the engine default are tinted with the accent colour; a `*` marks a modified tier's header.
- **Tier order:** a lower tier should never cost more than the next enabled tier above it (Low allowing GI that Medium
  skips, a longer shadow distance than High). Offending cells are tinted with the warning colour and listed under the
  table. Forcing cascaded shadows counts as the cheaper choice.
- Right-click a cell to reset it to the tier's default, or to apply its value to the higher, lower or all tiers.
  Right-click a tier header to preview it, copy settings from another tier or reset it.
- **Filter** narrows rows by name or group; **Modified only** hides rows every tier leaves at its default.
- **Scene viewport preview** (Off/Low/Medium/High/Ultra) applies a tier to the Scene viewport and follows edits to it
  live. Closing the project or switching it returns the viewport to scene settings.
- Undo/redo (Ctrl+Z, Ctrl+Y or the toolbar arrows) covers every edit; a drag is one step, and resets and copies are
  one step each. **More actions > Reset all tiers** restores the engine defaults.

| Row | Meaning |
|---|---|
| Resolution ceiling | 256–8192, powers of two; caps cascaded directional shadow-map resolution |
| Cascade ceiling | 1–4; caps authored cascade count |
| Distance ceiling | 1–10000 world units; caps directional shadow distance and positive caster-distance limits |
| Force cascaded shadows | On selects cascaded directional shadows; off preserves the authored method |
| Contact shadows | Allow screen-space contact shadows (sun and local lights) |
| Traced shadows | Allow local lights without a shadow map to be traced against the world |
| Foliage shadow sway | Allow swaying foliage to sway in the sun's virtual shadow maps (off: rest pose, cached pages) |
| Terrain anti-tiling | Allow terrain layers' stochastic tiling (off: plain tiling, one fetch per map) |
| Ambient occlusion / Global illumination / Screen-space reflections / Volumetrics | Allow SSAO; SSGI, DDGI and VCT GI; SSR; volumetric fog and clouds |
| Bloom and lens flare / Depth of field / Motion blur | Allow the corresponding effects |
| Foliage detail | 25–200%; caps instanced foliage detail |
| Grass density / Grass distance | 10–100% / 25–100% of each detail grass layer's authored values |

An allowed effect still requires scene authoring to enable it. Effects retain
their scene-authored strength, resolution and sample settings. Virtual-shadow
budgets, local shadow maps, material environment reflections, display settings,
texture residency, particles and non-foliage mesh LOD remain independent.

Edits write through to the project and mark it dirty; save with **File > Save Project** (Ctrl+S). Editing is disabled
during Play (preview still works). An already applied runtime policy is a snapshot: reapply a tier after editing it.

The rows come from `GetQualityTierFields()` (`editor/ui/.../quality/QualityTierFields.h`), a table giving each
`GraphicsQuality` member its label, group, range, control kind and cost direction. Editing, undo, order checks and
preview live in `QualityTierSession`, which reads and writes through a `QualityTierStorage` (the project's manifest
and the runtime's installed definitions); the panel only draws. To add a ceiling: add the member to `GraphicsQuality`
(with its range in `IsValid` and its tier defaults in `FromPreset`), apply it in the renderer, add a table entry,
extend the `QUALITY_PRESET` record and the scripting ABI.

Project manifests serialize four `QUALITY_PRESET` records, indexed 0–3, with
enabled, shadow resolution, cascade count, distance, the eight policy flags
(cascaded method, AO, GI, reflections, volumetrics, bloom, DOF, motion blur),
foliage detail, contact shadows, grass density, grass distance, traced shadows,
foliage shadow sway and terrain anti-tiling (21 fields). Records of 14, 15, 18,
19 or 20 fields from older projects load, taking the tier's engine defaults for the fields they
predate. Missing records retain engine defaults.
Invalid records reject loading with an error; invalid in-memory tiers reject
saving before opening the manifest for writing. Exports copy the definitions
into the runtime manifest. Editor project switches and standalone startup
install the definitions before scripts run. Player-selected tier persistence
remains owned by the game.

Open a viewport's **Quality** popup and select **Graphics preset**: Scene settings, Low, Medium, High or Ultra. Scene settings restores authored quality; a customised runtime policy displays Custom. This control requires the RHI preview.

Scene and Game viewports retain independent preview selections for the editor session. During Play, the Game viewport follows runtime quality, and changing its preset updates the runtime policy immediately, including what scripts read. Stopping Play restores its previous preview selection. The Scene viewport keeps its own selection throughout. Preview choices do not modify scene assets or the game's saved preferences.

## CoD

`GameGraphicsSettings` binds a selector to the title and pause menus. It cycles Low, Medium, High, Ultra and saves successful selections to `%LOCALAPPDATA%/PlutoCombat/settings.json`. The game migrates legacy `graphics.json` preferences. First launch defaults to High. Invalid/unreadable preferences fall back to High. Atomic file replacement keeps saved preferences intact if a write fails; the active selection still applies and a warning is logged. Opening either menu applies saved settings, including direct gameplay launch.

## Tests

`PlutoGEQualityTierEditorTests` checks the field table against `GraphicsQuality` validation, editing write-through, undo grouping, copies and resets, tier-order issues and preview lifetime. `PlutoGEGraphicsQualityProjectTests` checks legacy defaults (14- and 15-field records), every tier field and float roundtrips, grass scaling, runtime manifest preservation, runtime lookup, atomic table validation, invalid-save preservation and malformed records. `PlutoGEGraphicsQualityTests` checks tier validity, ceilings, contact-shadow gating, authored settings preservation, effect gates, custom overrides and reset. `PlutoGEGraphicsQualitySmokeTests` checks unsupported registration, the 28-byte wire layout, flag and foliage/grass roundtrips, managed validation and reset. Build ScriptCore and the native runtime together because the scripting bridge adds a required registration export.
