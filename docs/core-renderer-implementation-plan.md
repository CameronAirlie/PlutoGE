# Core renderer implementation

## Baseline and scope

The uncapped capture (11299–11538) reports 11.668 ms mean scene GPU time,
8.293 ms opaque/alpha-tested shading, and 0.570 ms VSM allocation per reported
observation. It still uses temporal upscaling at 490 × 231 → 980 × 461, so
motion output remains required. Effect algorithms and authored quality settings
are outside this change.

## Implementation sequence

1. Derive geometry output requirements from effect inputs, temporal reconstruction
   and debug views. Supply color-only, color+motion, full surface and diagnostic
   layouts, compatible shaders/pipelines, lazy optional targets, and safe fallback
   bindings. Retain optional targets once allocated until resize to avoid churn.
2. Cache geometry pipelines by output layout, material class, instance mode,
   sidedness and depth usage. Standard-material vertex variants omit the graph
   interpreter; color-only/depth variants omit motion history calculations.
   Preserve conservative cases for deformation and mixed instance transforms.
   Simple masked depth evaluates alpha without detail textures or lighting.
3. Add an exact material-coverage depth pass sharing the same vertex deformation
   and alpha evaluation. Shade covered opaque surfaces without depth writes and
   reuse current depth in the existing occlusion hierarchy. Keep transparency and
   scene-texture materials on their existing path. Expose reference switches for
   controlled comparisons, and separate depth draws from geometry submission stats.
4. Parallelize VSM page metadata updates and request/free-slot compaction. Preserve
   physical-slot preference, request ordering, coarse reservations, residency and
   dirty epochs; retain only small quota decisions on one lane.
5. Build the editor and both backend tests. Compare optimized and reference images
   for coverage, instancing, mirrored transforms, graph materials, debug views,
   temporal reconstruction and transparency. Run shadow allocation/residency tests.

## Architectural boundaries

Geometry resource and pipeline policy lives separately from the frame recorder.
New shader entry points reuse material evaluation rather than duplicating lighting
or coverage semantics. Optional shader packages retain the full-output fallback.

External consumers such as readback tools declare `BasicLighting::requiredGeometryInputs`.
Targets are produced when a consumer requires them; a getter alone does not request
rendering. The common color+motion path uses two color attachments (16 nominal
bytes/pixel), versus five (28 bytes/pixel) previously. Views needing surface
data conservatively retain the complete surface layout; diagnostics retain all six.

The RHI registry now uses address-stable storage: command contexts cache resource
pointers while recording, and lazy pipeline creation must not invalidate them.
Vulkan depth attachment barriers cover reads and writes in early and late tests.
Occlusion falls back conservatively if exact depth is unavailable and the legacy
two-sided occluder pass would disagree with material culling.

Coverage remains inline through `SurfaceCoverage.slang`. A discard-capable helper
returning material values introduced unstable derivative edge pixels on the test
GPU; the inline implementation passes the original shadow comparisons. Do not
relax image checks or reintroduce that helper abstraction without retesting them.

This implements a lean forward path first. A deferred/clustered rewrite is a
follow-on decision requiring diagnostic evidence of remaining lighting cost and
light counts; it is not bundled with changes that can be checked independently.
VSM's existing receiver depth has different coverage/projection requirements;
generalizing its reuse must preserve those contracts rather than aliasing an
incompatible depth texture.

## Validation status

Implemented and built in `RelWithDebInfo`, including `PlutoGEEditor`,
`PlutoGEVulkanRhiTests`, and `PlutoGEOpenGLRhiTests`.

- Vulkan renderer suite: 10/10 passed, including transparency, occlusion,
  diagnostic modes, optimization image comparisons, skinning, FSR2, VSM pool
  pressure, membership, and performance workloads.
- Additional cross-backend suite: 15/16 passed, covering handle stability,
  OpenGL renderer/optimization/shadows, and temporal motion, outlines and sky
  quadrature on both backends. Vulkan shader graphs passed. OpenGL shader graphs
  failed while compiling the separate legacy GLSL pass with
  `GL_MAX_TEXTURE_IMAGE_UNITS` exceeded. That legacy shader source is unchanged;
  the complete OpenGL shader-graph scenario is therefore not validated.
- Matched Vulkan synthetic overdraw benchmark: 32 overlapping lit planes at
  490 × 231, 36 GPU observations per configuration after warm-up. Complete
  geometry GPU scope (including coverage depth) fell from 2.05564 ms to
  0.335907 ms, approximately 6.1× faster, with identical final pixels.
  This isolates an overdraw-heavy case and does not predict the user's scene.
- The VSM allocator passes correctness and residency workloads; its isolated
  before/after speedup has not been measured.
- `git diff --check` passed. The existing user change to `CMakePresets.json`
  was preserved.

Build and test logs are under `out/build/renderer-final-build.log`,
`out/build/renderer-vulkan-final-tests.log`,
`out/build/renderer-vulkan-detailed-tests.log`, and
`out/build/renderer-cross-backend-tests.log`.

Next measurement: capture the same uncapped scene/camera with the rebuilt editor.
Compare total scene GPU time and complete `RHI Geometry` time, including the
new `RHI Geometry / Coverage depth` scope; comparing only shaded opaque time
would omit the cost moved into coverage. The profiler now records depth draw
count and color output count. Use `SetGeometryOptimizations` for controlled
output/depth/culling comparisons. Prepass cost can outweigh saved shading in
low-overlap or deformation-heavy scenes. Attachment bandwidth reduction alone
is not a frame-time prediction.
