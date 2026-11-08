# RocketLeg shader-graph and post-process performance pass — 2026-10-01

RocketLeg shades almost every opaque surface with the custom `Toon` shader graph,
which uses the **Direct Lighting** output. The RHI renderer interpreted graphs per
pixel (`ShaderGraphEvaluation.slang`), and that interpreter dominated the frame.
A first pass reduced geometry GPU time by 39% (68.6 → 42.0 ms) at 1600×900. A
second pass replaces interpretation with per-graph generated shaders on Vulkan:
the opaque pass falls from **42.7 to 6.5 ms** and the frame from **70.4 to 33.9 ms**.

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

## Generated shader-graph variants (second pass)

With the first pass applied, replacing `Toon` with `default-lit` on a scratch
copy reduced the opaque pass from 41.8 to 5.0 ms: the bytecode interpreter, with
its runtime-indexed `float4 registers[64]`, was still the dominant cost.

### Architecture

- **One definition of graph semantics.** `ShaderGraphCommon.slang` holds the
  bytecode layout, operand table and `shaderGraphInstruction`. The interpreter
  (`ShaderGraphEvaluation.slang`), generated code and the legacy GLSL path (CMake
  inlines the include) all use it.
- **Code generation** (`ShaderGraphCodegen.h/.cpp`). `GenerateShaderGraphSlang`
  emits straight-line code with the interpreter's API: one `const float4 rN` per
  instruction, calling `shaderGraphInstruction` with literal opcodes so the
  driver folds each call to its operation. The lighting stage carries only the
  surface registers it reads. Bytecode is validated (opcodes, topological
  operands, output registers) before emission.
- **Structure hash.** `ShaderGraphStructureHash` covers instructions, stage counts
  and outputs but not constant or parameter values, which generated code still
  reads from the material buffer. Materials that differ only in parameters (Arena,
  Ball, Blue, Orange, goals, pads) share one variant. It is cached on
  `ShaderGraphProgram::structureHash`.
- **Variant cache** (`ShaderGraphVariants.h/.cpp`). `Find` never blocks: a missing
  variant is queued for a worker thread, and the renderer keeps using the
  interpreter until it is ready. The worker writes `ShaderGraphGenerated.slang`
  and the SPIR-V atomically under
  `%TEMP%/PlutoGE/ShaderGraphVariants/<package>/<structure>/`. The package
  directory hashes the staged shader sources, the generator version and the
  compiler build, so stale variants are never reused. Failures are logged and
  leave that graph interpreted. One cache is shared per shader package.
- **Compiler backend** (`SlangShaderGraphCompiler.cpp`), behind the
  `ShaderGraphVariantCompiler` interface. It loads `slang.dll` dynamically, so
  builds or installs without it simply interpret, and compiles `BasicLit.slang`
  with `PLUTO_SHADER_GRAPH_GENERATED` using the build's settings (SPIR-V 1.3,
  column-major). CMake stages the shader sources in `shaders/source/` and copies
  the Slang DLLs beside the editor, runtime and Vulkan tests.
- **Renderer.** `GeometryPipeline` requests the `fragmentMain`, `colorMain`,
  `colorMotionMain` or `coverageMain` variant on Vulkan and keys pipelines by
  state plus graph structure. Draws that can discard switch coverage and shading
  together, so both passes always discard identically. The material-free opaque
  depth path is unchanged. The editor profiler reports
  `RHI shader graph draws: N specialised / M interpreted`.
- **Shader fix.** Slang's API linker faults (a null dereference in
  `getEntryPointCode`) when an entry point calls another entry point; the build's
  slangc path happens to avoid it. `colorMain`, `colorMotionMain` and
  `coverageMain` now call a plain `shadeFragment` helper instead of `fragmentMain`.
- `PLUTOGE_SHADER_GRAPH_VARIANTS=0` disables specialisation for A/B comparisons.

### Results

Same executable and shaders, alternating `PLUTOGE_SHADER_GRAPH_VARIANTS=0` and the
default, 600 frames after 300 warm-up frames (covering background compilation):

| Scope (mean ms) | Interpreter | Generated variants |
|---|---:|---:|
| Geometry / Opaque and alpha-tested | 42.45 / 42.88 | 6.46 / 6.46 |
| Geometry | 42.58 / 43.01 | 6.58 / 6.58 |
| Post Process | 24.15 / 24.28 | 24.40 / 24.27 |
| CPU frame | 69.91 / 70.82 | 33.93 / 33.82 |

### Validation

- `ShaderGraphVariantChecks.h` (Vulkan `--shader-graphs`): codegen invariants
  (parameters excluded from the structure, no register file, malformed bytecode
  rejected); cache semantics with a fake compiler (asynchronous resolution, disk
  reuse across instances, failures fall back); and GPU comparisons of the
  interpreter against generated variants for five graphs: parameters, toon Direct
  Lighting with a directional and two point lights, shared lighting registers, a
  graph covering nearly every opcode, and masked coverage. Each comparison
  asserts the draws used their variant.
- The renderer suites listed above pass on Vulkan and OpenGL.
  `PlutoGEOpenGLShaderGraphTests` still fails with the pre-existing
  `GL_MAX_TEXTURE_IMAGE_UNITS` link error, after the legacy GLSL compiles.

### Limitations and follow-ups

- Vulkan only. OpenGL, transparent/glass graphs (one shared transparent
  pipeline), shadow casters and VCT voxelization still interpret.
- Shipping: installs need the Slang DLLs and staged sources to specialise at
  runtime; no install rules were added. A cook step could precompile variants
  for each project graph through the same `ShaderGraphVariantCompiler` interface.
- The first use of a graph renders interpreted for the seconds that compilation
  takes; later runs load SPIR-V from the cache.

## Post-process investigation

First-pass build, 1600×900. Post Process is 24.3 ms and is unchanged by the
second pass.

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
