# DungeonCrawler rendering optimisation pass

This pass implements the measured GI/shadow opportunities from
`dungeon-rendering-architecture-investigation.md`. Production scene assets, lighting
settings, shadow resolution, update cadence, secondary bounce and gameplay remain
unchanged. Feature-removal diagnostic variants are not used for the final comparison.

## GI publication

Each cascade accumulates a dirty publication region from direct relighting. Resolve
expands it for the opacity stencil, and each directional mip receives the conservative
half-open parent region. New geometry, relocated volumes, secondary-volume completion,
gain changes, and shared gather snapshots retain full publication. Secondary influence
is deliberately not approximated by the smaller direct-light bounds.

Independent directional writes are batched between mip dependencies. The RHI now has a
compute-image barrier operation with Vulkan resource-scoped barriers and an OpenGL
image/texture barrier. Backends without a specialised implementation retain the broad
fallback. This is a focused resource-dependency API, not a new general render graph.
The six base destinations are retained for compatible sampling and storage-image limits;
this pass does not introduce a new multi-output format or change radiance filtering.

Full publication remains available as a diagnostic reference. The publication test
compares every frame of a moving-light sequence, including removal/replacement, secondary
gain, emissive edits, geometry movement/removal and field relocation. It also requires
less published voxel work. Existing world-cache and secondary-light tests protect the
actual lighting signal independently of that differential check.

## Virtual shadow planning

Persistent caster/page membership records cache conservative intersections. Caster
generations change with bounds; page generations change with allocation or relevant
projection changes. Bounds-only identity is intentional: equal bounds have equal
membership even if a different mesh takes the slot, while the existing ordered content
signature still detects mesh/material/animation changes. Growth clears new cache storage;
frame-epoch wrap resets membership, page metadata and asynchronous feedback together.

Signature work uses one cooperative group per page: lanes load/evaluate membership,
then the ordered hash and saturating triangle cost are reduced without changing their
semantics. Allocation and budgeting move global loads/stores to cooperative lanes and
keep the small policy decision sequence in workgroup memory. Retained-page order,
request order, per-level quotas, coarse priority, rotating fairness and greedy budgets
are preserved. This is deliberately not an unordered atomic allocator.

Budgeting emits a compact list of selected update pages; binning consumes that list and
the already evaluated membership rather than repeating every page/caster intersection.
The persistent cache costs 16 bytes per caster-capacity/page pair: 2 MiB at 512 caster
slots and 256 pages, up to 16 MiB at the supported 4,096-slot bound. The existing
shadow memory statistic includes this allocation.

The OpenGL implementation keeps the additional SSBO within the eight guaranteed binding
slots. Workgroup/image visibility is mapped to core OpenGL barriers rather than requiring
the optional memory-scope extension. Cached-vs-recomputed image tests cover movement,
deletion, slot reuse, reorder, unknown bounds, projection/camera changes and empty sets.

## Benchmark integrity

The runner can pin a compiled shader directory and records its package hash. This is
necessary because preserving an older executable alone would otherwise load the new
shaders from the build directory. The baseline is
`runtime/Release/PlutoGERuntime.pre-render-pass.exe`, with the saved
`out/build/render-pass-baseline-shaders` package. It includes only the shader-directory
override support, before any rendering changes. Both comparison arms reuse the same
captured managed assembly and run the original visual preset with all 16 enemies alive.

## Deferred architectural work

Static/dynamic shadow-depth layers, a full render graph, async compute, shared GI base
storage and GPU skinning are separate changes. They are not prerequisites for these
optimisations and would introduce new memory, dependency, bounds or motion-history
contracts. Reassess their value from the post-pass timings rather than combining them
into an unreviewable renderer rewrite. No quality tier or baked-lighting substitution
is introduced here.

## Final measurements

Six interleaved Release runs (baseline/candidate/candidate/baseline/baseline/candidate),
1280x720, 240 warmup frames and 1,800 measured frames each. Each arm contains 5,400
frames. No build/test work ran during capture. The GPU snapshots immediately before
and after this final batch reported 1% and 0% activity respectively; earlier captures
under competing GPU load are not pooled with these results.

| Metric | Baseline | Final | Change |
|---|---:|---:|---:|
| Mean frame time | 2.892 ms | 2.470 ms | -14.6% |
| Mean FPS | 345.8 | 404.8 | +17.1% |
| Frame p95 | 3.401 ms | 3.305 ms | -2.8% |
| GPU scene observations | 2.834 ms | 2.256 ms | -20.4% |
| GPU GI total | 0.754 ms | 0.532 ms | -29.4% |
| GPU GI publication | 0.407 ms | 0.220 ms | -46.0% |
| GPU shadow planning | 0.552 ms | 0.195 ms | -64.6% |
| GPU caster signatures | 0.193 ms | 0.015 ms | -92.0% |
| GPU geometry | 0.507 ms | 0.506 ms | Essentially unchanged |

All workload checks passed with 16 living enemies. Both arms have identical scene,
managed-assembly and post-process hashes; both average 53,902.89 skinned vertices,
97.17 geometry draws and 409.17 shadow submissions. Baseline runs span 343.0–347.3 FPS;
candidate runs span 403.5–407.2 FPS. Parent/child GPU scopes overlap and observations
can repeat asynchronously; they are not additive CPU-frame breakdowns.

These are standalone Release measurements on the RTX 4070 SUPER, not editor FPS or a
claim about every target GPU. Tail latency improves much less than the mean. GPU work
still occupies most of the frame, so GPU skinning is deferred: moving CPU work onto
the already busy GPU needs a separate measured decision and motion/bounds validation.

Run directories under DungeonCrawler `Tests/BenchmarkRuns`:

- Baseline: `20260923-202445-70e0fb43`, `20260923-202528-c308d7e3`, `20260923-202542-56e16c2e`.
- Final: `20260923-202501-3a5cacad`, `20260923-202515-825c68c0`, `20260923-202556-9354ee64`.

The executable/shader hashes, pooled statistics and run index are recorded in
`out/build/render-pass-final-aggregate.json` and `out/build/render-pass-final.json`.
The final shader package is pinned in `out/build/render-pass-final-shaders`.

## Fidelity and compatibility validation

The Vulkan and OpenGL full-vs-incremental GI sequences have **zero RGBA8 byte
difference** over 128 frames, while published voxel work falls from 19,660,800 to
10,570,954. Separate existing tests pass for world-cache lighting, secondary bounce,
live point/spotlight response, emissive coverage, temporal history and removed meshes.
The conservative region helper tests cover odd mip bounds, edge halos and empty unions.

The Vulkan and OpenGL membership checks match recomputed reference images in all 20
scenarios (14 visibly changing images). Existing directional/spotlight tests also pass,
including alpha masking, four spotlights with directional shadows, moving casters,
sloped receivers, shadow filtering, coarse coverage and overcommitted page budgets.
Allocation and budgeting use 128-lane groups and at most 16 KiB of shared storage;
they do not require the initial prototype's larger workgroup/shared-memory allowance.

The DungeonCrawler native combat fixture passes weapon attacks, bow draw/release,
projectile collision/piercing, shields, summons, equipped meshes, thumbnails and cached
3D portrait lifecycle, with clean shutdown. No authoring scene or production script was
changed. Visual validation is automated reference/coverage testing on this machine;
representative lower-end hardware still needs device-specific validation.

Release runtime and Debug editor/runtime builds pass. The VSM stress fixture also
passes stationary-cache convergence, moving-input invalidation and bounded submission
checks. Its diagnostic timings are not part of the performance table above. Build logs,
rendering-test logs and combat results are retained as `out/build/render-pass-*.log`.

The final Debug runtime smoke also exits cleanly with all 16 enemies. Its short
60-warmup/60-measured-frame capture is a launch check, not a matched performance
comparison: frame time is 13.063 ms while GPU scene time is 2.260 ms. Scene update
alone takes 5.415 ms and CPU recording 3.045 ms. Debug therefore remains CPU-bound;
the Release GPU gains must not be presented as equivalent editor/Debug FPS gains.
The capture is `20260923-203048-d40dca5e` under the benchmark runs directory.
