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
| 7 | Directional shadow mask at reduced cost | Planned | |
| 8 | Shadow LOD independent of the camera, with hysteresis | Planned | |
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
