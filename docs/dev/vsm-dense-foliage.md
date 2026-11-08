# VSM in dense foliage

Status: November 2026. Four changes to [virtual shadow maps](../virtual-shadow-maps.md), found on Verdant Gorge
(`ThirdPersonProject`, `Scenes/Verdant.plutoscene`):

1. per-instance page planning;
2. per-page sway;
3. clipmap levels that follow the shadow distance;
4. a static layer for dynamic casters.

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

## Remaining work

- **Camera-dependent caster churn.** While the gameplay camera moves, casters enter and leave the list, and change
  cluster LOD, through distance culling. That changes page content. Shadow LODs and shadow distances that do not
  depend on the camera (or that have hysteresis) would keep pages cached.
- **Resolution policy recovery.** Recovery needs nothing deferred. In a scene that always has deferred updates, the
  scale never returns to 1, even when the pool would fit.
- **Signature planning cost.** The pass went from about 0.08 to 0.3 ms. Every page still loops over every caster
  chunk, and the records are now 80 bytes.
- **Culling bounds.** `InstanceBinning` and `RhiSceneRenderer` still pad culling bounds with the any-wind bound.
