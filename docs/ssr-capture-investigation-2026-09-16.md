# SSR capture investigation, 2026-09-16

## Evidence

Compared supplied captures 76232–76471 and 78705–78944 (240 retained frames
each). All frames use normal rendering at 1690 x 934, VSync on and debugger
attached. The first has no SSR timing scopes and reports zero trace dimensions;
the second reports 845 x 467, 48 steps, five refinements and 16 rays throughout.
This is an SSR inactive/active comparison, not an optimisation before/after.

| GPU scope | Inactive mean | Active mean | Active p95 |
| --- | ---: | ---: | ---: |
| Scene | 4.022 ms | 6.139 ms | 6.678 ms |
| Geometry | 2.675 ms | 2.661 ms | 3.036 ms |
| Post process | 1.283 ms | 3.419 ms | 3.774 ms |
| SSR trace | — | 2.004 ms | 2.033 ms |
| SSR resolve | — | 0.330 ms | 0.645 ms |
| SSR total | — | 2.334 ms | 2.643 ms |

SSR occupies 38.0% of active scene GPU time; trace is 85.9% of SSR.
Scene time rises 2.117 ms, less than the SSR scope because other effects vary
between captures. Use the direct SSR scope for attribution. Parent scopes
include their children. GPU observations are asynchronous and may repeat;
these are descriptive statistics, not 240 guaranteed independent GPU samples.
CPU frame means (4.362 / 6.277 ms) include presentation waits and are unsuitable
for estimating SSR CPU execution cost or an uncapped FPS improvement.

## Implementation findings

The relevant RHI implementation is `engine/render/shaders/SSR.slang`, selected
by `BasicRenderer.cpp`; the embedded legacy GLSL in `SSREffect.cpp` is a separate
path. Stage-specific packages are loaded by `ShaderArtifacts.cpp`. The RHI
falls back to the combined shader if a specialised package is absent; captures
do not identify the selected package or establish the running binary revision.

Already implemented:

- Half width/height tracing followed by a four-tap, full-resolution resolve.
- Background, fully rough and back-facing receiver rejection; invalid ray
  direction rejection, offscreen termination and first-crossing termination.
- Per-ray projection setup, reverse-Z crossing comparisons and reuse of the
  final refinement depth sample.
- Separate trace/resolve shader specialisations.

There are 394,615 trace pixels. Before rejection and early termination, the
configured ceilings are 6,313,840 rays, 303,064,320 march depth samples and
31,569,200 refinement depth samples. These are not measured fetch counts.
The shader uses a fixed 16-sample GGX loop and has no SSR-specific history.
Every roughness below 1 remains eligible, despite the final `1 - roughness`
fade. Resolve samples depth, normal, material and reflection for each of four
neighbours and reconstructs their positions for tangent-plane rejection.

Prior work in `ssr-stage-optimisation.md` measured useful stage-specialisation
gains, but a conservative depth-hierarchy experiment was slower in both tested
workloads. `skinning-ssr-optimisation.md` also records no meaningful speedup
from removing a redundant final depth fetch. Neither is a new optimisation
opportunity to claim again.

## Recommended experiments

1. **Verify the running shader path and benchmark at this resolution.** Confirm
   specialised packages are present and selected. Extend/use the synthetic SSR
   fixture at 1690 x 934 and compare candidate/reference sequentially, reversing
   order on repeat. Preserve a baseline package before changing shared source:
   `--reference-stages` only disables specialisation; after a shared-source edit
   it does not restore the old algorithm.
2. **Try fixed-sample loop specialisation without reducing samples.** Compare a
   constant sample table and partial/full unrolling of the outer 16-ray loop.
   This could reduce loop/sample-generation overhead, but increased registers
   and code size could make it slower. Inspect generated shader code first:
   the compiler may already perform the useful transformations. Preserve ray
   order, sample values and hit semantics; validate images and timings before
   accepting anything. No speedup is established.
3. **If quality tradeoffs are acceptable, test adaptive ray counts.** Start with
   a configurable 16/8/4-ray comparison, then evaluate fewer rays on very smooth
   surfaces where GGX directions cluster. Regenerate the stratification and
   estimator denominator for each count; simply dropping samples from the
   current loop changes the sampled distribution. Expect possible banding,
   missed narrow features and reflection changes. Halving rays does not imply
   halving total SSR time, because setup, resolve and divergence remain.
4. **For a larger redesign, evaluate temporal SSR reconstruction.** Fewer rays
   per frame need dedicated reflection history, motion reprojection, depth/
   normal rejection, disocclusion handling, history clamping and camera-cut/
   resize invalidation. Existing final-frame TAA alone is not evidence that
   reduced-ray SSR will retain quality. This offers a broader work reduction
   than arithmetic cleanup, but adds passes, memory and ghosting risks.

Resolve is secondary: even eliminating it entirely saves only its measured
0.330 ms mean. Investigate its variable timing separately before replacing its
edge-aware reconstruction. Do not prioritise another hierarchy implementation
without a materially different traversal strategy and a costed experiment.

## Acceptance

For sampling-preserving changes, compare original/candidate readbacks using the
existing Vulkan and OpenGL SSR fixtures, including full/half resolution, odd
sizes, resize, roughness/material boundaries and refinement extremes. Then
capture the actual scene at the same view and settings. For sampling changes,
also inspect HDR reflection output and moving-camera sequences for shimmer,
thin geometry, disocclusion and metallic highlights; RGBA8 equality is no
longer the acceptance criterion. Report trace, resolve and total separately.

This investigation changes no shader or quality settings and makes no new
measured speedup claim. No rendering tests were run for this documentation-only
change; evidence is the supplied captures and source inspection.
