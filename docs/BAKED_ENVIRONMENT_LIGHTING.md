# Baked environment lighting

Baked ambient volumes ([ambient-probe-grids.md](ambient-probe-grids.md)) and reflection captures give low-end GPUs
ambient light, reflections and occlusion for well under a millisecond. Three additions keep them correct when the sky
or time of day changes:

| Feature | What it fixes | Where to set it |
| --- | --- | --- |
| **Live sky** | Baked volumes no longer freeze the sky at bake time. They follow the current physical sky, including time of day and night. | Ambient Light Volume > **Sky Lighting** |
| **Sky reflection occlusion** | Glossy surfaces indoors stop reflecting a sky their probes cannot see. | Ambient Light Volume > **Reflection Occlusion** |
| **Capture relighting** | Captures darken in corners their capture point could not see into, and follow the live sky. | Reflection Capture > **Relight From Ambient Volume** |

All three are on by default. They need no post-processing effect, so they work on every graphics preset, including
Low, which disables SSAO, GI and SSR. For a low-end target, combine them with material ambient occlusion maps and
contact shadows. SSAO and DDGI can be enabled on higher presets and work alongside them.

Probe volumes and reflection captures are the only baked lighting. Lightmap baking was removed: **Scene > Lighting >
Bake Lighting** bakes probes, and saved scenes that still reference lightmaps load without them. Meshes keep their
secondary UV channel (`uv2`) as ordinary geometry.

## Using it

1. Add an **Ambient Light Volume** around the playable space and a **Physical Sky** to the scene.
2. Leave **Sky Lighting** on **Live** and click **Bake Volume**. The inspector shows `Sky: live`.
3. Place reflection captures as usual. **Relight From Ambient Volume** is on for each one.
4. Change the sun direction or time of day. Volumes and captures follow without a rebake.

Settings:

- **Sky Lighting** (`Live`, `Baked`). Live stores only how much sky each probe sees; the renderer applies the current
  sky. Baked keeps the sky radiance as it was during the bake. Use Baked when the sky never changes and you want the
  bake to stay exactly as it was. A scene without a physical sky (an environment map) is always baked. Changing the
  mode requires a rebake; the inspector says so.
- **Reflection Occlusion** (0–1, default 1). How strongly the probes' sky visibility darkens reflections of the sky.
  It applies immediately, without a rebake. Lower it if a volume's coarse probe spacing darkens reflections near
  windows.
- **Relight From Ambient Volume** (reflection capture, default on). Turn it off for captures that must look exactly as
  captured, such as a capture of an emissive sign.

Scripts and code: `AmbientLightVolumeComponent::SetSkyLighting`, `SetReflectionOcclusion`,
`IblCaptureComponent::SetRelightFromAmbient`, and `SceneBakeSettings::probeLiveSky` for bakes without components.

## How it works

**Live sky.** The baker traces the same rays as before. A ray that leaves the scene is now recorded as sky
visibility, not added to the probe's SH. Visibility is stored per probe as a linear function of the surface normal:
`visibility(n) = w + dot(xyz, n)`, the cosine-convolved L1 SH of which directions reach the sky. For a probe above open ground it
is 1 facing up, 1/2 facing sideways and 0 facing down; in a sealed room it is 0. Each frame the CPU projects the physical sky
into L2 SH (`render/SkyIrradiance.h`) with the same sky model the baker samples. It reprojects only when the sky or
its sun changes. The shader adds `scale * visibility(n) * sky(n)` to the probe's SH light. `scale` is π times the
bake's bounce strength, so a live bake matches a baked one under the sky it was baked with: an unoccluded probe facing
up reproduces it to within a few percent (`PlutoGEIblCaptureTests`).

Only light arriving straight from the sky is live. Light bounced from surfaces, including the sun's bounce, stays as
baked. The factorisation `∫visible sky ≈ visibility × ∫sky` is exact for open probes and uniform skies, and close for
the smooth skies a physical sky produces.

**Sky reflection occlusion.** The probes' interpolated visibility is evaluated along the reflection's dominant
direction (GGX, leaning towards the normal for rough lobes) and scales the sky part of the environment reflection.
The reflection target's alpha keeps the remaining sky share, so DDGI's reflection occlusion only acts on what is left.

**Capture relighting.** Inside a volume, each capture's reflection is scaled by the luminance of the volume's
irradiance over the capture's own irradiance at the surface normal (Lazarov, *Getting More Physical in Call of Duty:
Black Ops II*, 2013). The factor is limited to 4: a capture made at night and shown at noon stays dimmer than it
should. Capture under representative lighting.

**Cost.** Sky visibility is packed into the spare `.w` channels of the SH rows the shader already loads, so it costs
no extra texture reads. The live sky adds nine uniform reads per pixel. On the forward path, which Low uses, there are
no extra passes. With the deferred probe resolve (layouts with albedo and indirect targets, for example with SSAO),
the half-resolution trace writes a second target with sky visibility. The composite then reads the environment
reflection and writes the adjusted one to a target that SSR and DDGI read. A layout without an environment reflection
target gains one in that case.

## Code map

| Piece | File |
| --- | --- |
| Bake data | `scene/BakedProbeVolume.h` (`skyVisibility`, `skyIrradianceScale`) |
| Authoring and persistence | `AmbientLightVolumeComponent`, `IblCaptureComponent` (volume bake data version 3; 1 and 2 still load) |
| Baking | `SceneBaker.cpp` `BuildProbeVolume` |
| Sky projection and visibility maths | `render/SkyIrradiance.h` |
| Atlas packing, per-volume parameters | `RhiSceneRenderer.cpp` (`AmbientVolumeSky`), `AmbientProbeVolumeParameters` |
| Probe sampling | `AmbientProbeSampling.slang` (`DiffuseGridSample`, `liveSkyRadiance`) |
| Reflection terms | `BakedEnvironment.slang`, used by `BasicLit.slang` and `DiffuseProbeResolve.slang` |
| Deferred resolve | `DeferredProbeIrradiance` (reflection pipelines and output target) |

To extend: new per-probe data goes in the atlas (`PLUTO_AMBIENT_PROBE_ROWS`; rows 6–9 still have free `.w`
channels). New per-volume data goes in `AmbientProbeVolumeParameters` (`worldToGridRows[2].w` is free). New
reflection terms belong in `BakedEnvironment.slang`, so the forward and deferred paths stay identical. Natural next
steps:

- A live sun bounce: bake the directional light's bounce into its own SH and scale it by the current sun colour and
  intensity.
- Baked per-vertex AO for small-scale occlusion without SSAO.

## Tests

- `PlutoGEBakedEnvironmentLightingTests`: visibility maths, sky projection against brute-force integration, the
  projection cache, scene lighting, and volume and capture persistence, including version 2 bakes.
- `PlutoGEIblCaptureTests` (Vulkan and OpenGL): live bakes leave the sky out of the SH. Sealed rooms see no sky. Live
  reconstruction matches a baked-sky bake. Mode changes ask for a rebake.
- `PlutoGEVulkanIblTests` / `PlutoGEOpenGLIblTests` (`CheckBakedEnvironmentRendering`): live sky brightness, enclosed
  probes, baked mode, sky reflection occlusion at strengths 0, 0.5 and 1, and capture relighting. Each check runs on
  the forward path and the deferred resolve, and the two paths must agree.
