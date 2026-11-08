# Directional shadow next-pass investigation

## Findings

- The post-change SSS capture has 240 normal-rendering frames, 603 x 346,
  69 draws and 169,606 submitted triangles. Geometry averages 5.318 ms versus
  5.355 ms before; this is not a demonstrated material project improvement.
- Saved `Assets/Scenes/Main.plutoscene` uses virtual shadows and softness 1.
  This matches the synthetic benchmark's softness, but does not prove the
  live editor had no unsaved changes. Current captures do not export softness.
- The benchmark shades a single flat, regular-grid receiver after a static
  cache converges. SSS has many meshes, alpha-tested geometry, an animated
  caster and continuing VSM page updates. It does not reproduce SSS coverage,
  derivatives, page crossings or fallback distribution.
- At softness 1 in the requested level, support is 2: normally a 4 x 4 tent,
  with fewer positive-weight taps at exact integer positions. Coarser fallback
  reduces support. The first pass preserved all those depth fetches.
- Generated GLSL retains dynamic level, mapping and filter loops, a local
  four-entry offset array, and one `textureLod` per filter tap. Compiling the
  same source to SPIR-V assembly also retains sampling and loop instructions.
  Neither intermediate representation establishes native GPU register use,
  occupancy, cache misses or the dominant stall reason.
- Alpha rejection already occurs before directional shadow sampling. Moving
  that rejection ahead of shadow sampling would not remove additional work.

## Recommended experiment: raw depth gathers

Prototype a same-page 2 x 2 gather path. A gather fetches four raw depths;
compare each with its own existing receiver-plane-corrected reference and
retain the current tent weights. For a full 4 x 4 footprint inside one page,
this can replace 16 scalar sampling instructions with four gather instructions.
It does not reduce the number of depth values or promise a fourfold speedup.

Do not replace this with hardware comparison filtering: each tap currently
uses a different receiver depth on sloped surfaces. Preserve scalar sampling
for page-crossing footprints and partial footprints initially. Preserve the
original row-major accumulation order and diagnostic nearest-texel value.
Handle gather component ordering explicitly, validated separately per backend.
Keep the current scalar path as the reference for image and timing comparisons.

A local Slang probe using `Sampler2D.GatherRed` compiles to `OpImageGather`
and GLSL `textureGather`. The compiler prints an implicit profile-upgrade
warning for SPIR-V, but the emitted probe declares only `OpCapability Shader`.
Validate the actual integrated artifact and runtime on both backends before
changing device capabilities or shipping the path. The probe establishes
compiler support, not rendering correctness or speed.

## Measurement and acceptance

1. Capture actual softness and derived maximum filter dimensions from the
   lighting used for each frame. Label those as configuration, not measured
   fragment counts. Use an optional diagnostic for page-crossing/fallback
   frequency; avoid always-on per-fragment atomics that distort timing.
2. Expand the benchmark with tilted receivers, overlapping alpha-tested
   geometry, moving casters and controlled page crossings. Include softness
   0, fractional softness, 1 and 4. Keep a matched scalar/reference variant.
3. Compare full filtered and raw-shadow images, including grazing surfaces,
   nonadjacent atlas tiles, missing pages and deferred refreshes. Preserve
   existing residency, coverage and update budgets.
4. Run repeated matched GPU comparisons. Reject the candidate if the extra
   branching/register demand offsets its sampling benefit or image checks fail.
5. Only then request another SSS normal-rendering capture. A native GPU
   profiler would be needed to make claims about occupancy or texture stalls.

## Investigation artifacts

Generated under `out/build`: `directional-shadow-current.spvasm`,
`directional-shadow-gather-probe.slang`, and its `.glsl` and `.spvasm` outputs.
No production shader or scene settings were changed during this investigation.
