# VSM signature planning

Step 4 of the [renderer optimisation roadmap](renderer-optimisation-roadmap-2026-11.md). `signatureMain`
(`shaders/VirtualShadowCompute.slang`) decides, for every requested page, which caster chunks reach it and whether its
cached depth is still valid. While the camera moves, page epochs change and most caster/page membership records must
be evaluated again, so this pass dominated VSM planning ([VSM in dense foliage](vsm-dense-foliage.md#remaining-work)
measured about 5 ms; on Verdant at 1280x720 with `--camera-travel=30` it was 16.4 ms).

## What changed

- **Page bounds once per page.** `intersects` transformed the page's six clip planes by the level's matrix for every
  caster, and for every model of a masked chunk (up to 16 more): six matrix-vector products per test. `PageBounds`
  holds the world-space planes and the root level's minimum caster radius, built once per page group; a test is six
  dot products. The page's sway test (`pageSways`) is likewise evaluated once per page.
- **Parallel signatures.** A page's signature was an ordered FNV hash that lane 0 folded item by item, behind two
  barriers per block of 64 casters. It is now the sum of a strong per-item hash (murmur3's finaliser over the caster's
  pose identity, model mask and sway mask), with static casters summed apart; costs are exact 64-bit sums that
  saturate to 32 bits as before. Each of 256 lanes accumulates its own casters and the group adds once. The
  signature depends only on the set of items, not on chunk indices: salting items with their index made pages dirty
  whenever casters entering the list shifted later chunks (14.3 instead of 11.7 page updates per frame).
- **Lazy caster reads.** Each lane reads a caster's identity (levels served, membership generation) first and its
  whole record only when its membership must be evaluated or it reaches the page.

The empty signature is unchanged (the FNV seeds), so pages that reach no caster keep their cached state.

## Measured

Verdant (ThirdPersonProject), 1280x720, `--camera-travel=30`, 300 frames after 120, Vega 8. One executable, the old
signature shader supplied through `PLUTOGE_SHADER_ROOT`, two alternating runs each:

| | Before | After |
| --- | --- | --- |
| `RHI VSM Planning / Caster signatures` | 16.6, 16.1 ms | 4.6, 5.2 ms |
| Scene GPU | 94.3, 92.9 ms | 88.0, 84.1 ms |
| Pages updated per frame | 11.7 | 11.7 |

## Tried and dropped

- **Block bounds.** Testing 64-caster blocks' union bounds before their casters was slower (16 to 21 ms): chunks are
  ordered for batching, not space, so blocks span the scene and rarely miss a page, and the extra dependent load at
  each block's head added latency.
- **Wider groups alone** (256 lanes with the serial hash) were slower: lane 0 then folded four times as many items per
  step while 255 lanes waited.

Neither the lazy reads nor the parallel hash measured faster by themselves; the plane transforms were the cost. They
stay because they remove the serial step and the redundant reads the pass would otherwise grow into with more casters.

## Checks

The VSM suites (`PlutoGEVulkanVsm*`, `PlutoGEOpenGLVsm*`, `PlutoGEVulkanRhiTests`), including the membership cache's
20 scenarios against its uncached reference on both backends, pass unchanged. `PlutoGEOpenGLVsmStaticLayerTests`
fails as it does at the baseline.

## Measurement note

Verdant benchmarks first ran against a runtime that failed every frame (`Invalid Vulkan texture binding`, see
[texture compression](../TEXTURE_COMPRESSION.md#replaced-textures)), which made its timings meaningless. Check
benchmark output for `failed to record` before trusting a CSV.
