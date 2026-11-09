# VSM in dense foliage

Status: November 2026. Changes to [virtual shadow maps](../virtual-shadow-maps.md), found on Verdant Gorge
(`ThirdPersonProject`, `Scenes/Verdant.plutoscene`):

1. per-instance page planning;
2. per-page sway;
3. clipmap levels that follow the shadow distance;
4. a static layer for dynamic casters;
5. page reuse order, planning cost, coarser LODs for coarse levels and chunking under the capacity (9 November,
   measured on the Vega 8; see [the second pass](#second-pass-page-reuse-planning-cost-and-caster-lods)).

## What went wrong in Verdant

All numbers are from `PlutoGERuntime --benchmark-project` at 2560x1440 on an RTX 4070 SUPER. The camera is the
gameplay camera, not fixed, so page requests vary between runs.

| Problem | Evidence | Fix |
| --- | --- | --- |
| A 16-instance chunk spans its whole foliage cell | Canopy cells (128 m) hold about 27 trees, so a chunk's sphere had a median radius of 63 m. Every page under the cell drew all 16 trees: up to 3.2M triangles per page. | [Per-instance planning](#per-instance-page-planning) |
| Wind padding far exceeded the actual sway | The any-wind bound (`FoliageWindMaxDisplacement(response)`) padded canopy trees by 30–40 m, about their own height. It inflated every chunk and model sphere, so the sway distance never excluded anything. | [Wind padding](#wind-padding) |
| One animated character redraws whole pages | Only 25 chunks changed per frame, all the skinned player. Every page they touch redrew its foliage too, at up to 770k triangles. Only about 8 pages fit each frame, so the update queue never drained and everything else aged behind it. | [Static layer](#static-layer-for-dynamic-casters) |
| Nothing beyond Shadow Distance is shadowed | The 4 fine levels were fixed fractions of the distance. At 142 m the outermost spans 284 m and nothing farther casts, and raising the distance coarsened every level. | [Clipmap levels](#clipmap-levels-follow-the-shadow-distance) |
| Shadows rendered at half resolution | The 576-page pool overflowed at 1440p, which set the resolution scale to 2. Recovery needs demand under a fifth of capacity with nothing deferred, which never happens here. | Scene setting: **VSM Pool Pages** 1024 (see [Verdant settings](#verdant-settings)) |

## Per-instance page planning

Caster chunks of at most `PLUTO_VSM_MASKED_INSTANCES` (16) models are planned per model:

- **Bounds.** `BuildCasterChunks` stores each model's world sphere (`CasterChunk::instanceBounds`, `ModelBounds`),
  padded by its wind reach. Preparation uploads them to `m_instanceBounds`: Vulkan binding 11, OpenGL storage slot 1.
- **Membership.** A caster/page membership record's `z` holds a mask with one bit per model whose sphere reaches the
  page (`intersectionMask`). The chunk's own bounds remain a prefilter. Rigid chunks and the 64-model whole-draw
  fallback keep a single bit.
- **Cost and signature.** A page's cost counts only the masked models' triangles. Its signature mixes in the mask, so
  a different subset of models is different content.
- **Binning.** A masked chunk lists one entry per (page, model), and its indirect instance count is the number of
  entries. The fallback chunks still list one entry per page, drawn for every model. An entry packs the physical slot,
  the chunk-local model and a sway bit (`virtualPageEntry`). The page vertex shader decodes it.
- **List size.** Page lists hold `page budget x 16` entries per caster, so they follow **VSM Page Updates per Frame**
  rather than the pool size. The page budget is part of the preparation key, so a changed budget reallocates them.

This needs no extra chunks, so scenes stay within `PLUTO_VSM_MAX_DRAW_CHUNKS`. Radius-capped CPU grouping was
rejected: in Verdant it would have needed about ten times as many chunks.

## Per-page sway

A page animates swaying casters only if it is within the wind's **Shadow Sway Distance** of the camera.

- **Per-page test.** `pageSways` tests the page against a sphere of that radius around the camera, in the light's view
  (`VirtualShadowParameters::culling.y`). A camera high above the ground therefore still counts as near the pages
  directly below it along the light.
- **Pose.** A page within the distance takes a caster's swaying identity (`identity.xy`) if the caster animates
  (`still.z`); otherwise it takes the rest-pose one (`still.xy`). Binning sets each entry's sway bit, and the vertex
  shader zeroes the wind for entries without it.
- **Coarse levels.** Coarse levels (the directional root and spot roots) always hold the rest pose. Their pages are
  large and expensive, and they are only a fallback.

### Why per page

Testing casters does not bound the cost, because one swaying tree redraws every page its crown reaches, and each of
those redraws all the page's casters. Two caster tests were measured in Verdant and both left most pages dirty:

- **Distance to the padded bounding sphere** (the original). Spheres on canopy trees reach tens of metres, so the
  camera stands inside many of them: 403 dirty pages at a 1 m distance, the same as swaying everywhere.
- **Distance to the plant's base.** 46 dirty pages at 1 m, but already 266 at 5 m and 408 at 30 m.

Testing pages also means a crown overhanging the camera sways whichever trunk it belongs to.

The trade-off is a seam: a shadow crossing the distance is posed differently on each side of a page boundary. It was
not visible in a still capture at 8 m, but it has not been assessed in motion.

### Measurements

Verdant at the spawn point under the canopy, 2560x1440, with 2M triangles per frame (adaptive). Requested pages vary
between runs, so compare the share of them that is dirty:

| Shadow Sway Distance | Dirty pages | Requested pages | Dirty share | Frames to refresh all dirty pages |
| --- | --- | --- | --- | --- |
| 1 m | 62 | 308 | 20% | about 6 |
| 5 m | 104 | 436 | 24% | about 13 |
| 10 m | 211 | 474 | 45% | about 26 |
| 20 m | 273 | 445 | 61% | about 39 |
| 30 m | 339 | 508 | 67% | about 48 |
| 60 m | 316 | 394 | 80% | about 45 |
| 0 (everywhere) | 284 | 347 | 82% | about 41 |

Under a canopy most visible pages are within a few tens of metres, so dense scenes need a much lower distance than
the 30 m default.

## Wind padding

Bounds are padded with `FoliageWindMaxDisplacement(wind, response, localTop)`: this wind's strength and gustiness, at
the geometry's actual height above the pivot. Clustered chunks are now padded too; before, only the whole-draw
fallback was. `VirtualShadowMaps` rebuilds every cached chunk when the wind settings change (`m_chunkWind`).

## Clipmap levels follow the shadow distance

`VirtualShadowFineLevels(distance)` (`VirtualShadowPolicy.h`) returns from 4 to `PLUTO_VSM_MAX_FINE_LEVELS` (8) fine
levels. The outermost level always spans twice the distance, and levels are added while the finest would span more
than `PLUTO_VSM_MAX_FINEST_SPAN` (64 m).

- **Existing scenes.** Distances up to 256 m keep the original four-level layout.
- **Longer distances.** 400 m gives five levels with the finest at 50 m; 1000 m gives six.

The coarse root now sits at level N, the active fine count (`VirtualShadowParameters::levels.x`,
`virtualRootLevel`), so directional levels stay contiguous. Unused slots lie between the root and the spot levels.
`PLUTO_VSM_ROOT_LEVEL` and `PLUTO_VSM_FINE_LEVELS` are gone: shaders and C++ use the root from the parameters, and
`PLUTO_VSM_LEVEL_GRID` takes it as an argument.

Verdant at 400 m (a scratch copy with the canopy's shadow distance also at 400 m), with the camera travelling:

- the same frame time as at 142 m;
- 16% more requested pages;
- five fine levels;
- distant canopy and gorge shadows that previously did not exist.

## Static layer for dynamic casters

Casters whose draws have no preparation revision are dynamic (`IsDynamicCaster`): skinned characters and other
transient draws. Once a frame has both static and dynamic casters, the static layer is created and kept until the pool
is replaced:

- **Storage.** `m_staticDepth` is a second D32 atlas in the same page layout, and a new generation starts
  (`levels.y`).
- **Pages.** Each page records the static casters' hash and the generation that layer was drawn under
  (`VirtualShadowPage::layer`).
- **Signatures.** The signature pass hashes static casters separately. An update redraws the static layer only when
  that hash, the generation or the page's validity changed (`freshness.w`); otherwise it costs only the dynamic
  casters.
- **Pass 1: static atlas.** The clear covers only tiles whose static layer is redrawn. Static chunks draw only into
  those tiles.
- **Pass 2: main atlas.** Every updated tile is restored from the static atlas (`VSMRestore`, an exact texel copy
  through `SV_Depth`, as `ShadowDepthSeed` does for cascades). Then the dynamic chunks draw.

Chunks are ordered static first, so each pass draws one contiguous range (`m_staticCasterCount`). Cost:

- one D32 atlas: 16, 36 or 64 MiB at the 256-, 576- and 1024-page tiers;
- one tile restore per updated page.

Scenes without dynamic casters do not allocate it.

`--vsm-static-layer` (both backends) checks two things:
- moving a dynamic quad over static grids redraws no static caster and submits fewer triangles than one of them;
- the result is byte-identical to the frame after the quad becomes static.

## Second pass: page reuse, planning cost and caster LODs

Measured on the user's Vega 8 (Ryzen 7 5700G), `PlutoGERuntime --benchmark-project` at 1280x720 with
`--frozen-render`, the scene's own quality, Verdant's startup camera (the trailhead), nothing else using the GPU. Builds
were compared against a frozen copy of the previous runtime, alternating runs. Images were compared with deterministic
`--capture`s of the review shots with the adaptive triangle budget off (it reads GPU timings, so captures would not
repeat); two captures of one build differ by about 56 dB PSNR.

### Page reuse order

A new page took the free physical slots in slot order, and a slot was free whenever its page was not requested this
frame. Jitter moves receivers across level boundaries, so pages leave the request set for a frame and come back. Those
pages sat at low slot numbers, ahead of the pool's empty slots, so they were overwritten and redrawn when they came back.
With 541 of 1024 pages requested, the static trailhead view evicted 22 pages and redrew 28 new ones every frame, up to
the triangle budget, and never converged. Pages still waiting showed as rectangular unshadowed holes (the gorge pool
review shot had several over the water).

`allocateMain` now orders the free list by `freeSlotPriority`: slots holding no usable depth (`pageRetained` false),
then pages unrequested for at least `VSM_STALE_PAGE_FRAMES` (32), then the rest. The allocator is otherwise unchanged
and still deterministic.

| Static trailhead | Before | After |
| --- | --- | --- |
| Evicted / new dirty / updated pages per frame | 22 / 28 / 14 | 0 / 0 / 0 |
| VSM planning + pages (GPU ms) | 8.9 + 6.5 | 0 (the reused frame records neither) |
| Scene GPU (ms) | 123.2 | 104.2 |

Three review shots match the old build within capture noise (50-57 dB); the gorge pool lost its holes.

### Planning cost

`signatureMain` evaluates every caster chunk for every requested page. Three changes, all with identical results:

- **Page-major membership.** A record's address is `slot * stride + caster` (`membershipRecord`, stride in
  `VirtualShadowParameters::levels.z`). Lanes are consecutive casters, so a wave reads one contiguous block instead of
  a record 16 KB apart per lane. Signatures went from 7.0-7.5 to 4.8-4.9 ms on the static trailhead.
- **Level test first.** A chunk not serving the page's level (a LOD variant) skips its membership record in both
  passes; `binMain` repeats the test before reading one, since planning never validated it.
- **Empty blocks skipped.** Lane 0 hashes a block of 64 chunks only when one of them reaches the page
  (`signatureBlockUsed`). A page reaches few chunks, so most blocks were 64 serial steps over empty items.

Testing whole-chunk bounds before the cache instead, to avoid reading it, was slower (8.6 ms): the cache saves the
intersection arithmetic, and the cost was the scattered reads.

### Coarser LODs for coarse levels

Foliage draws (cells) pick one LOD for the whole cell and drew it into every level. `RhiSceneRenderer` now gives
instanced shadow draws with LODs their mesh's `ShadowCasterLodChain` (`BasicDraw::shadowCasterLods`), and
`SelectShadowCasterLodVariants` (`ShadowCasterLods.h`) splits the levels between LODs: each level takes the finest LOD
from the draw's own whose whole-mesh triangles fit 0.15 per texel of the caster's disc (the camera passes' density), a
variant is kept only if it costs at most half the next finer one, and no level is finer than the draw's own LOD. Each
chunk carries the levels it serves (`VirtualShadowCaster::identity.w`); only the draw's own LOD splits into mesh
clusters, coarser variants plan each model whole.

On Verdant, fine-level texels are 3-49 mm, which every caster's own LOD (its shadow floor) already suits, so in practice
only the root (0.78 m texels) takes a coarser LOD: canopy trees drop from 5,102 to 485 triangles there. While the camera
moves, the root held about a third of the dirty triangles. Chunks are ordered fine-serving before root-only within the
static and dynamic ranges, so planning waves skip root-only chunks together.

### Chunking under the capacity

Variants need about 5,240 chunks on Verdant, against 3,852 without; the limit was 4,096 and Verdant used 94% of it. Above
the limit every caster fell back to whole-draw chunks of 64 unmasked models, so one page drew 64 trees: this is the
2.4 M-triangle page in the editor capture that started this pass. Now:

- `PLUTO_VSM_MAX_DRAW_CHUNKS` is 8,192, and buffers grow with the chunks needed (`VirtualShadowCasterCapacity`, steps of
  512 plus an eighth), not to the next power of two: membership records for Verdant's 5,240 chunks take 96 MB
  (6,144 slots x 1024 pages x 16 bytes), against 64 MB at 4,096.
- `SelectVirtualShadowChunking` steps down one level at a time: clusters with LOD variants, clusters, whole draws with
  16 masked models per chunk, whole draws of 64.

### Measured while the camera travels

`--camera-travel=30` (300 frames), so pages keep entering and leaving:

| | Before | Page reuse and membership | All |
| --- | --- | --- | --- |
| Dirty / deferred pages | 178 / 171 | 172 / 164 | 119 / 108 |
| Pages updated per frame | 7.5 | 7.2 | 11.4 |
| Largest dirty page (triangles) | 1.58 M | 1.52 M | 0.34 M |
| New dirty pages per frame | 104 | 84 | 53 |
| Caster signatures (GPU ms) | 9.1 | 5.8 | 5.6 |
| VSM planning + pages (GPU ms) | 9.9 + 5.2 | 6.5 + 5.1 | 6.3 + 4.3 |
| Scene GPU (ms) | 115.7 | 112.8 | 109.5 |

## Verdant settings

These are recommended scene changes; this work does not apply them.

- **VSM Pool Pages 1024.** At 1440p, Verdant needs about 830 pages at full resolution, and 576 forces the resolution
  scale to 2.
- **Shadow Distance.** Raise it to extend coverage (for example 300–400 m), and raise the canopy's
  `MaxShadowDistance` to match.
- **Shadow Sway Distance 5-10 m.** See [Measurements](#measurements). This is the main control for how quickly
  shadows update under the canopy.
- **VSM Page Updates per Frame** and the triangle budget. Page rendering takes about 0.2–0.3 ms, so there is GPU
  headroom. The adaptive budget does not grow while the scene runs within 10% of 16.6 ms, which is where it sat here.

## Benchmark columns

`--benchmark-project` CSVs now include:

| Column | Meaning |
| --- | --- |
| `vsm_updated_pages`, `vsm_caster_chunks`, `vsm_clustered_chunks` | Page updates and caster chunks this frame, and whether clustered chunks were used |
| `vsm_max_dirty_page_triangles`, `vsm_requested_pages` | Largest dirty page's cost, and pages requested |
| `vsm_dirty_fine_triangles`, `vsm_dirty_root_triangles` | Dirty triangles on the fine levels and on the root |
| `vsm_fine_levels`, `vsm_resolution_scale` | Active fine levels, and the resolution scale (above 1 means the pool overflowed) |
| `vsm_new_dirty_pages`, `vsm_changed_dirty_pages`, `vsm_static_dirty_pages` | Dirty pages that are new coverage, that changed content, and that need a static redraw |
| `vsm_evicted_pages`, `vsm_cache_hits` | Evictions and cache hits |
| `vsm_dynamic_chunks`, `vsm_static_layer` | Dynamic caster chunks, and whether the static layer exists |
| `vsm_chunking`, `vsm_lod_variant_chunks` | The chunking used (`VirtualShadowChunking`: 0 clusters with LOD variants, 1 clusters, 2 masked models, 3 whole draws), and chunks serving only some levels |

## Remaining work

- **Camera-dependent caster churn.** While the gameplay camera moves, casters enter and leave the list, and change
  cluster LOD, through distance culling. That changes page content. Shadow LODs and shadow distances that do not
  depend on the camera (or that have hysteresis) would keep pages cached.
- **Resolution policy recovery.** Recovery needs nothing deferred. In a scene that always has deferred updates, the
  scale never returns to 1, even when the pool would fit.
- **Signature planning cost.** Every requested page still loops over every caster chunk (about 5 ms on the Vega 8
  while the camera moves). Binning casters to pages from each caster's footprint would make it proportional to the
  pairs that meet.
- **Fine-level caster cost.** Fine levels draw each caster's own (floored) LOD; L4 holds the most dirty triangles
  while the camera moves. Per-instance LODs, as cascades have, would help there.
- **Culling bounds.** `InstanceBinning` and `RhiSceneRenderer` still pad culling bounds with the any-wind bound.
