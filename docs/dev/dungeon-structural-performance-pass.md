# DungeonCrawler structural performance pass

## Retained architecture

The renderer gathers unique mesh/pose pairs from visible and shadow commands before translating draws. A bounded persistent skinning executor divides aggregate vertex work across up to four participants (including the calling thread), then joins once. Small workloads retain a serial path. Frame scratch buffers retain capacity. Graphics uploads remain on the render thread.

The existing deformation kernel remains authoritative. Geometry/shadow deduplication, bounds, previous positions, pause/history clearing and per-viewport caches are preserved. Tests cover uneven batches of individually small meshes, empty jobs, one/four-participant execution, independent reference results, aliased history and bounds.

Scene runtime discovery now uses a scene-owned component index. Membership changes invalidate the index; reparenting invalidates its traversal order. Enabled and parent-active state are evaluated at use time. Fixed/late/start/stop callbacks use snapshots with registration-generation handles, so removing a later script cannot leave a dangling callback target or accidentally dispatch a new object at a reused address. Snapshot membership excludes components inactive at the beginning of a phase.

Audio, fixed-step physics and physics-query discovery reuse indexed component lists. Physics retains configuration-signature validation and reconstructs the full active entity list only when rebuilding the simulation world. This deliberately avoids assuming every mutable physics property publishes a trustworthy revision. Simulation frequency and callback hierarchy order remain unchanged.

## Investigation decisions

The initial paired Release skinning comparison measured 3.746 ms mean / 4.925 ms p95 before and 3.123 ms / 3.569 ms after. Treat this as an initial pair; final repeated comparisons are recorded separately.

The GPU capture identified VCT GI (~0.81 ms), virtual-shadow planning (~0.58 ms), and geometry (~0.54 ms) as substantial scopes. Receiver requests alone were ~0.02 ms. These are asynchronous, nested observations; they do not sum to an independent frame budget. VCT publication occurs within its existing progressive update/lighting workflow, so it was not bypassed or throttled to manufacture higher FPS. No rendering quality settings changed.

A transform-anchor HUD prototype passed projection/resize/scale checks but increased traversal cost. Hiding unused anchor subtrees reduced the regression, yet the final prototype still measured HUD 1.179 ms mean / 2.574 ms p95 versus 1.120 ms / 2.350 ms for the original in the adjacent comparison. The production HUD change was discarded. Benchmark HUD subscopes remain for future diagnosis.

The conditional animation rewrite was deferred: Release animation components measured about 0.09 ms, and retargeting already reuses most scratch arrays. Rewriting pose ownership would add risk without addressing the dominant Release cost. Animation update frequency and fidelity remain unchanged.

## Measurement and remaining limits

Validation: Debug editor and Debug/Release runtime builds succeeded. The skinning CPU/reference/batch, Vulkan skinning, attachment, collider CCD, foliage collision, scene-history/index lifecycle, scene streaming and benchmark CSV checks passed. The DungeonCrawler native enemy fixture also passed 175 checks across all 15 prefabs, imported clips, attacks, pause, shields and summons, with a clean process exit. The combat fixture passed weapon attacks, bow/projectile collision, shields, summons, equipped meshes and portrait lifecycle checks.

The benchmark now writes named GPU scope observations and separates HUD synchronisation, update/layout and rendering. Duplicate scope names within one GPU observation are summed. CPU scopes can include waits; GPU results may repeat and are not aligned with CPU rows.

Initial comparisons used 120 warmup and 600 measured frames. Final comparisons use the preserved pre-pass Release executable, identical captured managed scripts, 16 living enemies, 1280x720, 240 warmup frames and 1,800 measured frames per run. Build/test work finished before timed runs. The fixture excludes editor overhead and player attacks, and disables Streamline because of its separately identified shutdown stall.

## Final measurements

Six interleaved runs used baseline/candidate/candidate/baseline/baseline/candidate order. Each arm contains 5,400 measured frames. The table pools frame samples for frame mean/p95 and averages the per-run subsystem means.

| Metric | Baseline | Candidate |
|---|---:|---:|
| Mean frame | 4.572 ms | 4.487 ms |
| Frame p95 | 6.295 ms | 6.246 ms |
| Mean FPS | 218.7 | 222.8 |
| CPU skinning | 1.518 ms | 0.514 ms |
| Scene update | 0.667 ms | 0.667 ms |
| Audio | 0.043 ms | 0.037 ms |
| GPU scene observations | 4.466 ms | 4.422 ms |

Skinning is 66.1% faster in elapsed time. Whole-frame FPS is only 1.9% higher; this is not a verified substantial gain. Independent `nvidia-smi` snapshots showed 29–43% GPU activity with no PlutoGE process running, with other GPU clients including Rocket League and Wallpaper Engine. They were not closed by the benchmark. Both arms are predominantly graphics-limited under these conditions, and the earlier larger single-pair FPS improvement must not be generalised.

Both arms had identical scene and managed-assembly hashes, 53,902.89 skinned vertices, 97.17 geometry draws and 409.17 shadow draws on average. Every workload check passed with 16 living enemies. The saved project scenes and rendering preset were not changed.

Final run directories under DungeonCrawler `Tests/BenchmarkRuns`:

- Baseline: `20260923-190834-80a5ca6f`, `20260923-190929-3a47bbdf`, `20260923-190947-7e89c80e`.
- Candidate: `20260923-190853-88712abc`, `20260923-190912-863ce89d`, `20260923-191005-b83ec503`.

The candidate-only 640x360 diagnostic (`20260923-191104-214f0bbc`) measured 3.102 ms / 322.4 FPS, with 2.260 ms GPU scene time. This is resolution sensitivity evidence, not a same-quality optimisation comparison. A final default Debug capture (`20260923-191122-b57ef201`) measured 11.848 ms / 84.4 FPS; no matched pre-pass Debug binary was available, so no controlled Debug improvement percentage is claimed.

The final candidate's large GPU scopes include VCT GI at 1.211 ms, VSM planning at 0.903 ms, and geometry at 0.813 ms. VCT volume publication totals 0.638 ms per observation. These scopes include nested work and reflect the background load. A future GPU pass should investigate incremental GI publication/lighting updates and shadow signature/allocation work while preserving coverage and image quality. First repeat with the competing GPU workloads paused to establish an uncontended baseline.

After CPU skinning is reduced, the GPU scene time is close to the total Release frame time. Further large gains require reducing measured GI/shadow/geometry work or changing the rendering architecture. Another CPU micro-optimisation cannot reliably double the frame rate once GPU work limits throughput. Validate any future GPU change on representative lower-end hardware and a combat workload, not only this RTX 4070 SUPER fixture.
