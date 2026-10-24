# Rendering performance: tooling and the opaque/post-process pass

How to measure GPU cost per feature, and what the 2026-10 optimisation pass
changed. Numbers are from the ThirdPersonProject startup view at 1280×720
(`--frozen-render`, Vulkan); compare like with like.

## Measuring

### Headless project benchmark

```
PlutoGERuntime --benchmark-project <project> <out.csv> [frames] [warmup] --frozen-render
               [--geometry-diagnostic=<mode>] [--capture=<frame.png>]
```

* `<out.gpu.csv>` lists every GPU scope (mean, p95, max). Nested scopes such as
  `RHI VCT GI / Cone trace` sit inside their parent; do not add them together.
* The CSV's `parallel_recording`, `descriptor_cpu_ms` and `descriptor_binds` columns show whether shadows and geometry
  recorded on worker threads and what descriptor work cost (see [parallel-recording.md](parallel-recording.md)).
* `<out.cpu.csv>` lists the scene update's CPU cost per component type, animation
  phase, script `OnUpdate` and script `OnLateUpdate` (same columns). A script's
  row includes whatever it calls, so a spawner's max shows its `Instantiate` cost.
* `--geometry-diagnostic` applies one shading diagnostic to every measured
  frame and requires `--frozen-render`. `--scalar-shadows` remains an alias
  for `scalar-shadows`.
* `--capture` writes the last presented frame as an upright PNG (Vulkan only).
  Frozen renders are deterministic, so two captures of one mode match exactly
  and any difference between modes is real.

### Geometry diagnostics

Modes live in one table, `PlutoGE/render/GeometryDiagnostics.h`: the enum
value, a stable command-line token and the editor label. The editor
profiler's Comparison mode list reads the same table. To add a mode, append
an enum value (never renumber, since BasicLit reads it as `pointParameters.w`),
add a table row, and branch on it in the shader.

| Token | Measures |
| --- | --- |
| `minimal-shading`, `material-only` | Lighting cost versus rasterization and material cost |
| `bypass-directional-shadows`, `scalar-shadows` | Directional shadow filtering |
| `bypass-sky-lighting`, `reference-sky` | Physical sky environment lighting |
| `bypass-reflection-captures` | Box-projected reflection captures |
| `bypass-probe-grid` | Baked diffuse probe grid |
| `reference-probe-grid` | Forward probe grid (deferred resolve disabled), for A/B checks |
| `bypass-detail-textures`, `bypass-point-lights`, `draw-ranges` | As named |

Before this pass the sweep attributed the 11.8 ms opaque pass as follows:
probe grid 5.5 ms, sky lighting 2.7 ms, directional shadows 2.3 ms, captures
0.5 ms, materials and rasterization about 1 ms.

## Changes

### Coverage-aware environment lighting (BasicLit)

Environment layers are evaluated most specific first: reflection captures,
then the probe grid, then the physical sky, then flat ambient. A layer is only
evaluated where it can show. Sky irradiance is skipped inside probe volumes
(the grid replaces it), and the sky is skipped entirely under full capture
coverage. The output is unchanged. Sky lighting cost went from 2.7 ms to
0.35 ms.

### Deferred probe irradiance

`DeferredProbeIrradiance` (`engine/render/src/`) with
`DiffuseProbeResolve.slang`. Inside a probe volume, an opaque surface's diffuse
environment light is always `gridIrradiance × diffuseResponse`, even under
captures. When the geometry layout has albedo, material, normal and indirect
targets, BasicLit skips the grid and marks the pixel with `normal.a = 0`.
A half-resolution trace then evaluates the grid. A full-resolution composite
upsamples it with plane and normal weights, falls back to exact evaluation
where no reduced sample matches, and adds irradiance × response to scene
colour and to the indirect target that SSAO occludes. It uses premultiplied
blending with zero alpha, which is additive.

The grid cost went from 5.5 ms in the forward pass to 1.2 ms. Against
`reference-probe-grid` the image differs by a mean of 0.02 / 255; 0.012% of
pixels differ by more than 4 / 255 (silhouettes only). Transparent surfaces and
layouts without those targets keep the forward path.

`SceneLightingParameters.slang` now holds BasicLit's frame uniform block so
screen-space passes share one declaration with the forward shader.
`PostProcessParameterBlock.h` is the CPU mirror of `PostProcessParameterBlock`.

### VCT GI

* History metadata (normal, view depth, coverage) is written by the temporal
  resolve as a third output. The separate full-screen metadata pass and its
  shader were removed.
* History and metadata targets are RGBA16F instead of RGBA32F. History alpha
  was unused, and the metadata depth test tolerates 1% of view depth.

Temporal resolve plus metadata went from 2.14 ms to 0.98 ms (VCT total
4.33 ms → 3.19 ms). The image differs from full precision by a mean of
0.009 / 255.

### Volumetric clouds

`CloudNoiseVolume` bakes the shader's lattice hashes into a tiling 128³ RGBA8
volume. `cloudNoise()` replaces eight integer hashes and seven interpolations
per octave with one trilinear fetch at `floor(p) + smoothstep(fract(p))`.
Production modules compile `VolumetricCloudBaked.slang`. The procedural
`valueNoise` remains the reference for noise-continuity probes. The lattice
tiles every 128 noise cells (about 21 km at the default base scale), so cloud
layout is a different random instance of the same noise. Cloud cost went from
2.9 ms to 1.5 ms. The component's Render Scale now selects the RHI trace
resolution (0.5 = half, the previous fixed behaviour). OpenGL samplers now
wrap the third texture axis, matching Vulkan.

## Results

| Pass (ms) | Before | After |
| --- | --- | --- |
| Opaque geometry | 11.76 | 4.91 |
| Deferred probe irradiance | � | 1.21 |
| VCT GI | 4.26 | 3.19 |
| Volumetric clouds | 2.93 | 1.49 |
| Scene GPU frame | 25.5 | 17.7 |

## Second pass

### Directional shadow level transitions (BasicLit)

A VSM receiver inside a level transition band (clipmap edge or footprint
change) used to filter two levels and blend them. With a temporal resolve
active (TAA, an upscaler or shared camera jitter), `temporalClipOffset.z` is
set and each pixel instead filters one level, chosen by interleaved gradient
noise that changes every frame (`temporalClipOffset.w`); accumulation restores
the blend. Both choices share one call site so a wave never runs the tent
filter twice. A non-resident neighbour falls back to the requested level, as
before. Without temporal accumulation the exact blend is kept. The forward pass
went from 4.88 ms to 4.40 ms. The converged image differs by a mean of
0.001 / 255.

Measured split of the remaining directional shadow cost (about 1.8 ms): 0.95 ms
of level selection, page lookups and setup, and 0.83 ms of tent filtering
(softness 1.5 needs nine gathers).

### VCT cone trace interleaving

Reduced-resolution traces store each checkerboard half compacted to half
width, in two targets. Once the history is valid and lighting and settings are
stable, a frame traces only one half and rasterizes only its fragments; the
temporal resolve reads both halves and down-weights the stale one. Both halves
are traced after a settings change, during reactive lighting, without history
and in debug views. `cacheSettings.w = 0` selects an uncompacted trace. The
cone trace went from 1.8 ms to 1.0 ms. The image differs from full tracing by a
mean of 0.006 / 255.

### TAA history split

History colour is RGBA16F. Layered depth moved to its own R32F target, read
with exact texel loads. TAA went from 1.9 ms to 1.7 ms; the image differs by a
mean of 0.06 / 255.

### VSM page allocation

The allocator's round-robin spare-page loop stops when a round grants nothing,
and request counts are read once. Page allocation went from 0.52 ms to
0.06 ms (live run).

### Immutable buffers (Vulkan)

Immutable, non-uniform buffers with initial data are placed in device-local
memory through a staging copy. No change on integrated GPUs; this targets
discrete GPUs, where host-visible vertex data crosses PCIe.

### Results

| Pass (ms) | Start of pass | After |
| --- | --- | --- |
| Opaque geometry | 4.91 | 4.40 |
| VCT GI | 3.19 | 2.52 |
| TAA | 1.9 | 1.67 |
| Scene GPU frame | 17.7 | 15.5 |

## Next candidates

* **SSAO (1.85 ms).** The raw pass costs about 0.37 ms fixed plus 0.2 ms per
  16 samples. With temporal accumulation, 16 samples instead of 32 changes
  the converged image by a mean of 0.015 / 255 and saves 0.2 ms; it is a
  per-project setting, so check it under motion. The six depth-pyramid
  dispatches (0.26 ms) could be fused into one or two.
* **Directional shadow setup (about 0.95 ms).** Level selection transforms the
  receiver once per candidate level; clipmap levels share a rotation, so one
  light-space transform plus per-level scale and offset would suffice.
* **Clouds (1.5 ms).** Temporal amortisation needs a cloud history with
  reprojection by a representative cloud depth, plus rejection for wind,
  lighting and volume changes. The trace currently has no history by design;
  ghosting has to be validated with camera motion, not frozen frames.
