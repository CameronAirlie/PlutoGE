# RocketLeg shader-graph and post-process performance pass — 2026-10-01

RocketLeg shades almost every opaque surface with the custom `Toon` shader graph,
which uses the **Direct Lighting** output. The RHI renderer interprets graphs per
pixel (`ShaderGraphEvaluation.slang`), and that interpreter dominated the frame.
Two retained changes reduce geometry GPU time by **39%** (68.6 → 42.0 ms) at
1600×900. The interpreter still costs roughly 37 ms; see "Remaining work".

## Workload and measurement

- AMD Ryzen 7 5700G with integrated AMD Radeon Graphics, Windows, Vulkan.
- `PlutoGERuntime --benchmark-project samples/RocketLeg/RocketLeg.plutoproject
  <out.csv> 600 120` (RelWithDebInfo), 1600×900 native, no upscaler, VSync off.
- Arms alternate baseline/candidate twice. Executables and shader directories
  were snapshotted per arm and selected with `PLUTOGE_SHADER_ROOT`.
- GPU scope means come from the runtime's `.gpu.csv`; parent scopes include
  children and must not be added together.

## Retained changes

1. **Opaque graph depth uses the material-free depth shader.**
   `GeometryDepthResources` returned `Full` for every graph material, so the
   coverage prepass ran the complete lit fragment shader. An opaque fragment graph
   cannot change depth or coverage (the VSM path already relies on this), so an
   opaque graph without vertex offsets now uses `GeometryDepth`, like a standard
   surface. `coverageMain` also returns for `alphaMode == 0` before any graph
   evaluation. Masked/blended graphs and vertex-deforming graphs are unchanged.
2. **Direct Lighting reuses surface registers.** Every light previously re-ran
   the whole graph program from instruction 0. `ShaderGraphState` now carries the
   register file and pre-graph inputs from the surface stage, and
   `evaluateShaderGraphLighting` executes only the instructions after the surface
   count. The CPU compiler already emits lighting instructions after the surface
   stage, reading its registers, so the results are identical.

| Scope (mean ms) | Baseline | Candidate |
|---|---:|---:|
| Geometry / Coverage depth | 6.97 / 6.52 | 0.07 / 0.08 |
| Geometry / Opaque and alpha-tested | 62.05 / 61.44 | 41.01 / 42.66 |
| Geometry | 69.09 / 68.02 | 41.14 / 42.80 |
| CPU frame | 95.92 / 93.84 | 67.55 / 69.78 |

Post-process scopes were unchanged between arms (within 0.4 ms).

## Validation

- Vulkan and OpenGL: RHI, render-optimization, geometry-diagnostic, occlusion,
  VSM-only, transparency, opaque batching, temporal motion and outline suites
  pass. `PlutoGEVulkanShaderGraphTests` passes, including toon directional/point
  light bands, two accumulated point lights and a branch shared by Albedo and
  Direct Lighting.
- `RenderOptimizationChecks.h` adds two scenarios against the no-prepass
  reference renderer: an opaque Direct Lighting graph with directional and point
  lights (lean prepass plus register reuse), and the same graph masked (graph
  coverage retained).
- `PlutoGEOpenGLShaderGraphTests` fails at legacy shader link time with
  `GL_MAX_TEXTURE_IMAGE_UNITS`. The unmodified baseline fails identically; this
  predates the change (also noted on 2026-09-24).

## Remaining work: the interpreter itself

With the same candidate build, replacing `Toon` with `default-lit` on a scratch
copy of the project reduces Opaque and alpha-tested from **41.8 ms to 5.0 ms**.
The interpreter loop and its runtime-indexed `float4 registers[64]` remain the
dominant cost. On GCN/Vega a 256-VGPR dynamically indexed array cannot stay in
registers; it is very likely placed in scratch memory (inferred, not confirmed
from driver ISA). Options, in increasing effort:

- **Register allocation in the graph compiler.** Instruction index currently
  equals register index. Liveness-based reuse would let Toon (~35–40
  instructions) fit a small register file (for example 16), which drivers can
  keep in VGPRs. Requires a destination field in `ShaderGraphData`, which is
  shared with C++, the legacy GLSL path, shadows and VCT voxelization.
- **Generated shaders per graph.** Emit Slang per graph hash, compile offline or
  at cook time, and keep the interpreter as a fallback while compiling. This
  removes the interpreter for shipped content; it needs pipeline caching keyed
  by graph hash and an editor compile path.

## Post-process investigation

Candidate build, 1600×900. Post Process is 24.3 ms.

| Effect | GPU ms | Finding |
|---|---:|---|
| SSR | 5.8 (trace 5.15) | Traces 16 GGX rays × 48 steps for every receiver with roughness < 1. RocketLeg's floor (0.82, dielectric) and walls (0.95) receive under 1% reflection. |
| Transparency | 5.3 | Full-screen depth copy, colour snapshots (about one full frame of pixels), then the full lit glass shader over a full-screen roof plus walls. |
| Volumetric clouds | 5.2 | Half resolution, 24 primary × 3 light steps. Each density sample evaluates ~32 integer-hash value-noise lookups; 32-bit integer multiplies are quarter rate on GCN. |
| TAA | 2.6 | ~50 fetches per pixel: 9-tap Catmull-Rom history, 3×3 colour+depth+normal neighbourhood, closest-depth search. |
| SSAO | 2.4 | Half resolution, 16 samples, plus resolve and composite. |
| Bloom | 1.5 | Six iterations. |

Measured experiments (scratch shader variants, not retained):

- **SSR roughness cut-off** (skip trace when roughness > 0.6): SSR trace
  5.15 → 1.78 ms, Post Process 24.3 → 20.9 ms. Productising this needs a fade
  band to avoid a visible edge, and an exposed "Max Roughness" parameter.
- **Glass blur taps skipped below half a pixel** (StadiumGlass blur radius is
  ~0.08 px): Transparency 5.30 → 5.05 ms. The cost is not in the blur taps;
  add GPU sub-scopes for snapshot copies and pane shading before optimising
  further.

Recommended order: SSR max-roughness with fade; baked tiling 3D noise texture
for clouds (one or two fetches per density sample instead of ~32 hashes); glass
sub-scope timings, then reduce per-pane lighting cost; TAA 5-tap Catmull-Rom and
cross-shaped neighbourhood. Re-measure after each change; percentages above
apply only to this workload and GPU.
