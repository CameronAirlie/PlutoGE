# Prepassed coverage: early depth for alpha-tested surfaces

Step 5 of the [renderer optimisation roadmap](renderer-optimisation-roadmap-2026-11.md).

## Why this instead of wider GPU-driven submission

The roadmap's step 5 was wider GPU-driven coverage: masked draws and the depth prepass through multi-draw indirect,
and occlusion for every mesh. Measured first on the target machine (Vega 8, frames GPU-bound):

- Main-pass occlusion culling (`OCCLUSION_CULLING Cull` against `Off`): Verdant 79.9 to 79.5 ms, Settlement 47.2 to
  47.0 ms scene GPU. Its own cost is 0.1-0.4 ms; hidden single meshes are not where the time goes.
- Multi-draw submission saves CPU recording time, which a GPU-bound frame does not show.
- Verdant's geometry pass spent 17.5 ms in the coverage depth prepass and 32.5 ms shading opaque and alpha-tested
  surfaces, almost all of it foliage cards and blades.

The shading pass of an alpha-tested surface kept its alpha test's `discard` even after the depth prepass had applied
it. A shader that can discard cannot have depth tested before it runs, so every fragment foliage rasterized, including
those behind nearer leaves, was shaded and then rejected.

## What changed

- `kSurfacePrepassedCoverage` (constant 8, `SurfaceFeatures.slang`) removes the alpha test's discard from
  `SurfaceCoverage.slang`.
- `BasicRenderer::GeometryPipeline` sets it, with an **equal** depth test, for prepassed masked standard surfaces:
  their prepass ran the same alpha test through the same vertex transform (standard coverage shaders), so depth
  equality selects exactly the covered fragments. The shading pass's usual greater-or-equal test would let a hole in a
  nearer card shade over the surface behind it once the discard is gone.
- Opaque surfaces never discarded; graph materials, impostors, depth-only pipelines and OpenGL (no specialisation
  constants) are unchanged. `BasicRenderer::SetPrepassedCoverage(false)` or `PLUTO_PREPASSED_COVERAGE=0` keeps the
  discard.

## Measured

Vega 8, `--frozen-render`, two alternating runs each, one executable toggled by `PLUTO_PREPASSED_COVERAGE`:

| Scene | `RHI Geometry` before / after | Scene GPU before / after | Captures |
| --- | --- | --- | --- |
| Verdant, 1280x720 | 35.9 / 30.8 ms | 79.9 / 74.8 ms (-6.4%) | 89.5 dB, no pixel off by more than 48 |
| Wind Fields, 1920x1080 | 9.7 / 8.6 ms | 18.7 / 17.5 ms (-6.4%) | 80.8 dB, no pixel off by more than 48 |

## Not done

- The prepass itself (17.5 ms on Verdant) still draws every visible card; its cost is vertex work and the alpha test.
- Graph materials with alpha keep the discard: their coverage shader evaluates the graph, and depth equality would
  need the same guarantee from it.

## Checks

`PlutoGEVulkanPrepassedCoverageTests` (`--prepassed-coverage`): staggered checkerboard cards over a ground plane render
the same image with and without the discard.
