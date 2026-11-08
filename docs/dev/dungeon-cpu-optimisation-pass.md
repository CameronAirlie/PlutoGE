# DungeonCrawler CPU optimisation pass

## Scope and contracts

This pass targets the CPU costs in capture 26496–26735. It preserves render resolution,
lighting, shadows, animation sampling rate, skinning, collision shapes and gameplay
update frequency. The original capture averaged 12.323 ms on the CPU versus 3.672 ms
for the scene GPU; editor/debugger timings are not interchangeable with standalone
Release measurements.

- Entity world rotation/scale decomposition is cached together and invalidated by
  the existing recursive transform invalidation, including ancestor changes and
  reparenting. Reflection and singular-scale semantics are unchanged.
- Activity queries traverse ancestors once. IsActiveInHierarchy previously called
  recursive IsActive at every ancestor, revisiting the same hierarchy repeatedly.
- Animation binding compiles a parent-before-child node schedule. Poses use a linear
  evaluation pass instead of recursive std::function calls and a fresh visit bitmap.
  TRS composition writes scaled rotation columns and translation directly instead
  of multiplying three general matrices. Blending, masks, retargeting and cadence
  remain unchanged; malformed cyclic node edges are treated as roots.
- Stable render-command slots retain their cached packets when one animated actor
  changes. Full value/revision validation still authorizes reuse; the slot shortcut
  does not assume that repeated mesh instances, transforms or motion history match.
  Visible/shadow skinning lists are collected together once, with an additional
  collection only for GI-only poses.
- Physics query membership changes retain unchanged Bullet shapes and broadphase
  proxies. Added/changed shapes are constructed individually and obsolete proxies
  are removed. Empty shapes retain membership records to prevent repeated rebuilds.
  Actual entity deletion still invalidates before freeing pointers; foliage retains
  the existing full rebuild path. This is the query world, not a rewrite of the
  dynamics/ragdoll world.
- RmlUi retains unchanged per-draw uniform data. Updated transforms, clipping and
  resize-dependent parameters remain compared on every draw; texture bindings
  are still issued for each draw. A separate
  transform-anchor HUD layout experiment was rejected after measuring a regression;
  production markup, positioning and styles remain unchanged.
- Reinforcement prefabs are preloaded during session creation without spawning live
  actors. New scopes distinguish enemy summon spawning, prefab resolution and
  hierarchy/component construction. This removes first-use content loading from
  combat, but does not promise elimination of every spawn or dynamics-world hitch.
- Editor profiling selects the last rendered viewport's scene renderer rather than
  always reading the editor viewport's stale state. Diagnostic settings are applied
  to both viewport renderers.

## Validation

SceneCpuCacheTests covers transform invalidation, activity, mirrored/nonuniform
animation against a matrix reference, shuffled parent order, draw-cache invalidation,
and collision insertion, deactivation/reactivation, shape edits, deletion and empty
shapes becoming valid. Existing skeleton, foliage and Vulkan/OpenGL UI tests cover
attachments, collision fallback and rendered antialiasing/clipping behaviour.
The managed game suite passed 848 assertions.

## Measurements

Twelve interleaved standalone runs: baseline/candidate/candidate/baseline/baseline/
candidate for each of Debug and Release, 240 warmup and 1,800 measured frames per run
(5,400 frames per arm/configuration). Resolution 1280x720, VSync off, fixed simulation
and the same courtyard orbit. All runs passed workload checks and exited cleanly.
No build or other test process ran during the comparison.

| Build | Baseline mean / FPS | Candidate mean / FPS | FPS change | Baseline p95 | Candidate p95 |
|---|---:|---:|---:|---:|---:|
| Debug | 10.592 ms / 94.4 | 9.148 ms / 109.3 | +15.8% | 15.206 ms | 13.066 ms |
| Release | 2.592 ms / 385.8 | 2.492 ms / 401.3 | +4.0% | 3.694 ms | 3.484 ms |

Frame percentiles pool all measured frames, rather than averaging run percentiles.
Debug runs varied: baseline 82.5–102.5 FPS, candidate 99.1–115.7 FPS. The final Debug
pair was slower in both arms; no run was discarded. Median individual-run FPS was
101.0 versus 114.8. These measurements support the CPU reduction, not a guarantee
of identical editor FPS on every run. Editor UI/debugger overhead is additional.

| Debug CPU scope | Baseline | Candidate | Reduction |
|---|---:|---:|---:|
| Scene update | 4.373 ms | 3.694 ms | 15.5% |
| Animation components | 1.146 ms | 0.875 ms | 23.6% |
| Physics | 0.821 ms | 0.559 ms | 31.9% |
| Render translation/preparation | 1.979 ms | 1.412 ms | 28.6% |
| Runtime HUD | 0.957 ms | 0.923 ms | 3.6% |

The HUD saving is small and within run variability; it is not the source of the
larger CPU improvement. Release gains are smaller because scene GPU time remains
about 2.27 ms. Lower-end hardware has not been measured.

All arms match scene, shader-package and post-process hashes. Both configurations
and arms average 53,902.89 skinned vertices, 97.17 geometry draws and 409.17 shadow
submissions, with at least 16 living enemies throughout. Managed assembly hashes
differ intentionally because of reinforcement preload/instrumentation; production
HUD markup/styles, visual presets and authored gameplay tuning are unchanged.

Reproduction: `out/build/run-cpu-pass-comparison.ps1`, using saved
`PlutoGERuntime.pre-cpu-pass.exe` binaries for both configurations. Raw run manifest:
`out/build/cpu-pass-comparison.json`; pooled results: `out/build/cpu-pass-aggregate.json`.
Every manifest entry links its `summary.json`, `frames.csv` and staged assets.

## Native combat and shutdown validation

The candidate passed all 175 enemy-pack assertions (15 prefabs, imported and rendered
attack poses, impacts, pause, shields, bounded summons and reward rules), but exceeded
the fixture's ten-second native shutdown grace period. The saved baseline executable
and original game scripts reproduce the same timeout after all 175 assertions pass.
This is a pre-existing teardown issue, not counted as a passing end-to-end QA run.
All twelve benchmark processes did exit cleanly. Baseline isolation is recorded in
`out/build/cpu-pass-enemy-baseline-result.json`; candidate and baseline native logs
are retained under `out/build/cpu-pass-enemy-*`.
The engine graphics API integration test also passed, as did SceneCpuCacheTests in
Debug and Release and the Vulkan/OpenGL UI tests.
