# Renderer optimisation roadmap (November 2026)

A survey of the renderer on 2026-11-10 ranked the large optimisations still missing, by expected gain on the target
machine (Ryzen 7 5700G, Vega 8 iGPU: bandwidth- and pixel-bound) and on discrete GPUs. Work already done or measured
and rejected is listed in `renderer-modernisation.md`, `frame-pipeline.md`, `rendering-performance.md`,
`vsm-dense-foliage.md` and `renderer-structural-review.md`; this roadmap does not repeat it.

Each step lands as its own commit, keeps the previous path behind a switch, and adds checks on both RHI backends.

| # | Step | Status | Doc |
| --- | --- | --- | --- |
| 1 | Block-compressed material textures (BC1/BC3/BC5), encoded in the background and cached by content | Done | [TEXTURE_COMPRESSION.md](../TEXTURE_COMPRESSION.md) |
| 2 | Automatic resolution scaling from GPU time | Done | [DYNAMIC_RESOLUTION.md](../DYNAMIC_RESOLUTION.md) |
| 3 | Deferred local lights for standard opaque surfaces | Done (neutral on Settlement; see doc) | [deferred-local-lights.md](deferred-local-lights.md) |
| 4 | VSM signature planning: per-page plane setup, parallel order-independent signatures | Done (signatures 16.4 -> 4.9 ms on Verdant) | [vsm-signature-planning.md](vsm-signature-planning.md) |
| 5 | Geometry coverage: early depth for prepassed alpha-tested surfaces (rescoped: occlusion measured negligible) | Done (-6.4% scene GPU on Verdant and Wind Fields) | [prepassed-coverage.md](prepassed-coverage.md) |
| 6 | Bandwidth: DDGI apply gathered at half resolution (the largest measured full-screen pass) | Done (apply 5.9 -> 2.8 ms, -4.4% scene GPU on Verdant) | [DDGI.md](../DDGI.md#half-resolution-gather) |
| 7 | Directional shadow mask at reduced cost | Investigated, no change (see [Step 7](#step-7-findings)) | |
| 8 | Shadow LOD independent of the camera, with hysteresis | Investigated, no change: already provided (see [Step 8](#step-8-findings)) | |
| 9 | Froxel volumetric fog with temporal reuse | Planned | |

## Design rules for every step

- New behaviour sits behind an interface or a policy object that can be replaced without touching its callers
  (for example `TextureBlockEncoder`, `SelectCompressedFormat`).
- Capabilities are queried from the device (`IRenderDevice::Supports*`), never inferred from the API name; the
  fallback path stays the previous behaviour.
- Pure decision logic (policies, controllers, planners) has no device dependency, so CPU tests cover it.
- Image-equivalence or tolerance checks on Vulkan and OpenGL accompany every rendering change.

## Step 6 scope

Smaller target formats (motion RG32F to RG16F, R11G11B10 post targets) and discard load operations were measured
against the frame's largest full-screen passes first. On Verdant the post-process segment was 27.7 ms, of which the
DDGI apply alone was 5.9 ms, every pixel gathering up to three cascades of eight probes along two directions. Halving
that pass's resolution was worth more than the format changes, which save a few megabytes of writes per frame on a
desktop GPU without tile memory; they remain open (`RenderingInfo` has no discard load operation, and the RHI no
two-channel half-float format).

## Step 7 findings

The directional shadow mask costs about 2.7 ms on Verdant at 720p. Two cheaper receivers were measured and neither
moved it:

- selecting the clipmap level in light space before filtering, rather than from the receiver's footprint;
- taking the receiver's derivatives from the full-screen pass's own fragment quad (`quadDepthReceiver` in
  `DepthReceiver.slang`) instead of the neighbour search.

The pass is bound by the filter's texture latency, not by receiver reconstruction. What remains is a reduced-resolution
mask with a depth-aware upsample, which would soften shadows on thin foliage (grass blades are a pixel or two wide), so
it is left as a quality decision rather than shipped as an optimisation. `quadDepthReceiver` stays as the shared
receiver of the deferred local light pass, which used to inline it.

## Step 8 findings

The virtual shadow maps already draw instanced casters at a LOD chosen per clipmap level from the level's texel size
(`SelectShadowCasterLodVariants`, `ShadowCasterLods.h`), not from the camera: Verdant builds about 4,490 LOD-variant
chunks a frame. A hysteresis on the remaining camera-derived caster LOD (`SelectShadowCasterLod`) was implemented and
measured on Verdant with a 30 m/s camera travel:

| | Changed pages / frame | New pages / frame | Scene GPU (median) |
| --- | --- | --- | --- |
| Without hysteresis | 68.8 | 135.1 | 80.5-82.1 ms |
| With hysteresis | 68.7 | 134.9 | 81.8-82.5 ms |

It held 0.1% of selections (about 1,700 of 1.6 million across 2,100 casters). With the camera still, 50 pages a frame
still change content, from wind-swayed foliage; the rest of the travel cost is pages newly in view as the clipmaps
scroll. Neither depends on caster LOD, so the hysteresis was removed rather than kept as code with no effect.
