# Rendering performance: tooling and the opaque/post-process pass

How to measure GPU cost per feature, and what the 2026-10 optimisation pass
changed. Numbers are from the ThirdPersonProject startup view at 1280×720
(`--frozen-render`, Vulkan); compare like with like.

## Measuring

### Headless project benchmark

```
PlutoGERuntime --benchmark-project <project> <out.csv> [frames] [warmup] --frozen-render
               [--geometry-diagnostic=<mode>] [--capture=<frame.png>] [--serial-render]
               [--quality=low|medium|high|ultra] [--camera-pan=<degrees>] [--camera-travel=<metres>]
               [--disable-effects=<Type>[,<Type>...]|all]
```

* Frames record on the render thread unless `--serial-render` is given (see [frame-pipeline.md](frame-pipeline.md));
  render statistics in a sample then describe the previous frame, the last one completed.

* `<out.gpu.csv>` lists every GPU scope (mean, p95, max). Nested scopes such as
  `RHI VCT GI / Cone trace` sit inside their parent; do not add them together.
* The CSV's `parallel_recording`, `descriptor_cpu_ms` and `descriptor_binds` columns show whether shadows and geometry
  recorded on worker threads and what descriptor work cost (see [parallel-recording.md](parallel-recording.md)).
  `uniform_bytes` and `persistent_uniform_bytes` count uniform data copied into the frame's arena, and
  `vsm_page_batches` the virtual shadow page draws issued (see [frame-pipeline.md](frame-pipeline.md)).
  Packet-preparation columns split command translation (`visible_preparation_ms`, `shadow_preparation_ms`,
  `gi_preparation_ms`, `batching_ms`, `rebuilt_draw_packets`, `opaque_batch_*`, `visible_packets`). The `vsm_*`
  columns report whether VSM was active, the caster packets it rebuilt, and its delayed dirty and deferred page and
  triangle counts. `ray_traced_rebuilt` reports whether the DDGI top level was rebuilt. The run also prints the
  directional shadow status, so a VSM that is unavailable is visible.
* `<out.cpu.csv>` lists the scene update's CPU cost per component type, animation
  phase, script `OnUpdate` and script `OnLateUpdate` (same columns). A script's
  row includes whatever it calls, so a spawner's max shows its `Instantiate` cost.
* `--geometry-diagnostic` applies one shading diagnostic to every measured
  frame and requires `--frozen-render`. `--scalar-shadows` remains an alias
  for `scalar-shadows`.
* `--quality` applies a project graphics tier before the scene loads. Frozen
  renders run no scripts, so without it they use the scene's authored quality
  rather than whatever tier a game's settings would select.
* `--camera-pan` swings the base camera's yaw by +/- that many degrees over a
  120-frame cycle, after the scene update, so temporal reconstruction (TAA,
  upscalers) is measured and captured under motion.
* `--camera-travel` moves the base camera +/- that many metres along its horizontal forward axis over a
  600-frame cycle. Use it to measure costs that only appear as the camera travels, which panning alone does not
  exercise: LOD changes, producer shadow distances, VSM page churn and the ray-traced GI scene.
* `--disable-effects` turns off the named post-process effects (type names such as `SSAO`, `DDGI`; `all` for
  every one) on the rendering camera each frame, exactly as the editor's Enabled toggle does, so a run per effect
  shows what it costs. A disabled effect records no GPU work at all (`PlutoGE{Vulkan,OpenGL}DisabledEffectTests`).
* `--capture` writes the last presented frame as an upright PNG (Vulkan only).
  Frozen renders are deterministic, so two captures of one mode match exactly
  and any difference between modes is real.

### Shader registers (Vulkan, AMD)

`PLUTO_VULKAN_SHADER_STATS=1` enables `VK_AMD_shader_info` where the driver offers it and prints, for each graphics
pipeline as it is created, the vertex and fragment stages' VGPRs, SGPRs and scratch bytes. On GCN a SIMD runs
floor(256 / VGPRs) waves, so a shader above 128 VGPRs runs one wave and cannot hide texture latency. Scratch is
register spill or a dynamically indexed local array; every fragment that reaches it reads and writes memory.

### Contention

GPU timestamps include other processes' work that the scheduler interleaves. Close the editor, and any other GPU
client, before benchmarking: with two editor windows open, Wind Fields measured 87.6 ms where the same build and
settings take 45.7 ms alone. When frame time is far above `scene_gpu_ms`, or a scope is inexplicably large, suspect
contention before the renderer.

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

A third pass, aimed at integrated GPUs, is in
[igpu-lighting-performance-pass.md](igpu-lighting-performance-pass.md).

## Fourth pass: surface specialisation and caster LODs (Wind Fields, Vega 8)

Measured on `samples/WindFields` at 1920×1080, live (wind animating, no `--frozen-render`), Vulkan, Ryzen 7 5700G
(Vega 8), nothing else using the GPU. Medians of 180 frames after 90 warm-up frames; "before" is the same build with
each change switched off.

### Surface specialisation (BasicLit)

BasicLit is one uber shader: every local light type, reflection captures, probe volumes, both directional shadow
methods, and clear coat and sheen. Branches on frame data skip unused paths at run time, but the driver allocates
registers for the heaviest path. Every lit geometry fragment shader used 192–208 VGPRs (one wave per SIMD) and spilled
about 12.7 KB to scratch, so an ordinary grass pixel stalled on every shadow and sky fetch.

Vulkan geometry pipelines now specialise those paths away (`src/SurfaceFeatures.h`, `shaders/SurfaceFeatures.slang`).
The frame's features come from the same values its uniforms carry (local light count, capture and probe-volume
presence, shadow method); clear coat and sheen come from the draw's material. A path is removed only where its data
would skip it anyway, so images are identical (`CheckSurfaceSpecialization` in `PlutoGEVulkanRhiTests`). OpenGL
compiles the defaults, the general shader, and keeps one pipeline per state as before.
`BasicFrameStats::surfaceFeatures` reports the frame's set; `BasicRenderer::SetSurfaceSpecialization(false)` turns it off
for comparisons.

The specialised Wind Fields pipelines use 122–126 VGPRs (two waves) and no scratch. A pipeline compiles on first use
of a new feature set, for example when the first local light appears, so expect one compile hitch per feature set.

`atlasOffsets` in the directional VSM filter was indexed by loop counters, so it lived in scratch memory. The corner
loops now unroll and the reference path selects with constant indices; the image is unchanged.

### Cascade caster LODs

Instanced shadow-cascade casters with a LOD chain (foliage) drew every instance at the draw's single LOD, so a foliage
cell beside the camera cast all of its clumps at full detail into every cascade. Cascades now draw each 64-instance
chunk at the LOD the camera passes give it, merging adjacent chunks at the same LOD; instances the camera hands to an
impostor cast at the coarsest mesh LOD. The runs join the draw's shadow signature, so retained cascades redraw when a
run changes LOD. This needs `SupportsFirstInstance` (Vulkan); elsewhere draws cast at their own LOD as before.
Selecting the runs costs about 1 ms of render-thread CPU on Wind Fields (some 80,000 shadow instances).
`BasicRenderer::SetShadowCasterLods(false)` restores whole-draw casting. VSM caster chunks are unchanged: forcing the
coarsest LOD there saved under 0.6 ms on this scene, where trees dominate the page triangles.

### Results

| Preset (GPU ms) | Before | Specialised | + scratch fix | + caster LODs |
| --- | --- | --- | --- | --- |
| Low (cascades) | 15.6 | 14.6 | 14.6 | **12.4** |
| Medium (cascades) | 22.0 | 20.7 | 20.5 | **17.7** |
| Default (VSM, SSAO/SSGI off) | 29.6 | 28.7 | **27.7** | 27.7 |
| Default with SSAO and SSGI | 45.7 | **39.6** | | |

Specialisation matters most with SSAO or SSGI. Their G-buffer layout keeps more values live through the shader, and
at one wave per SIMD that cost about 25 ms of geometry pass on this view; specialised, the layouts differ by a few
milliseconds.

## Fifth pass: shadow mask, interleaved clouds, still shadow pages (Wind Fields, Vega 8)

Same setup as the fourth pass: `samples/WindFields` at 1920×1080, live, Vulkan, Vega 8, nothing else on the GPU,
default preset with SSAO and SSGI off.

### Directional shadow mask

Filtering the directional shadow in the surface shader cost about 8 ms of the 12.7 ms geometry pass: with it compiled
out, the pass took 4.8 ms and the shader 88 instead of 126 VGPRs. The filter ran on every lit fragment, at two waves
per SIMD, and on grass a large share of those were helper lanes on 1–2 pixel wide blades.

`DirectionalShadowMask` (`src/DirectionalShadowMask.h`, `shaders/DirectionalShadowMask.slang`) now evaluates it once
per pixel of the coverage depth, after the shadow maps and before shading, into an R32 mask. Prepassed shading
pipelines are specialised to read one texel instead (`kSurfaceShadowMask`, constant id 6). Their fragments are exactly
the coverage depth's, because the shaded pass tests depth for equality, so the mask is always valid for them and those
pipelines carry no fallback filter at all. Transparent, outline and diagnostic pipelines keep filtering in place.

- **One filter.** The VSM and cascade code moved out of `shadeSurface` into `evaluateDirectionalShadow`, which both
  the surface shader and the mask pass call. The mask pass is compiled from BasicLit, so there is no second copy of
  the filter to keep in step.
- **Receivers from depth.** The world position comes from the inverse of the jittered view-projection the coverage
  depth was drawn with. Each screen derivative comes from the neighbour on the same surface: the one nearer in world
  space. A sliver whose nearer neighbour is still more than 12 pixel footprints away (a distant blade with no
  neighbour on its surface) takes the view-facing plane instead, rather than a step to another surface.
- **Receiver orientation.** The receiver plane now faces the camera in both paths. Before, the surface shader turned it
  toward the interpolated vertex normal, which on authored foliage normals (vertical on grass, outward on leaf cards)
  need not be the side in view. For ordinary single-sided geometry the two are the same.
- **Agreement.** On Wind Fields the mask image differs from in-shader filtering by a mean of 0.6/255, with 1% of pixels
  beyond 16/255, all at blade silhouettes. `CheckDirectionalShadowMask` (`PlutoGEVulkanRhiTests`) compares the two with
  virtual shadow maps and with a fitted cascade.
- **Scope.** Vulkan only: OpenGL has no specialisation constants to select the mask. `BasicRenderer::
  SetDirectionalShadowMask(false)` restores in-shader filtering; `BasicFrameStats::directionalShadowMask` reports it.
  The pass shows as `RHI Geometry / Directional shadow mask`.

### Interleaved cloud trace

While a cloud volume's history reprojects, `CloudTemporalFilter::PlanTrace` interleaves its trace: a half-by-half trace
updates one pixel of each 2x2 block of the history per frame, cycling through the block every four frames (diagonals
first). The resolve blends traced pixels into the history and keeps the others from the reprojected history.

- **Weight.** A traced pixel takes `InterleavedWeight` (0.15), not the full-rate weight's four-frame equivalent. That
  would average a quarter of the samples and double the noise. The history follows a change over about 24 frames
  instead of 10.
- **Clipping.** Traced pixels clip the history to this frame's neighbourhood at 1.25 σ, as before. Skipped pixels clip
  at 3 σ (`SkippedGamma`): a box of samples two pixels apart, offset by up to a pixel, would otherwise pull them toward
  their neighbours (measured: error 1.82 instead of 0.74 against the dense reference).
- **Jitter.** The per-pixel step jitter advances once per period (`TraceJitterFrame`), so each pixel still sees a
  golden-ratio sequence of offsets.
- **Appearance jumps.** `ObserveAppearance` restarts a volume's history when its lighting, colour, density or shape
  parameters jump by more than 5% in a frame (a light switched, a preset applied), so the change shows at once.
  The wind drift is excluded, and gradual changes such as the time of day still accumulate.
- **Checks.** `CheckCloudTemporal` requires the interleaved trace to converge within 1.75 times the full-rate error
  (0.74 against 0.48); `SetCloudTraceInterleave(false)` traces every pixel.

### Still shadow pages for swaying casters

- **Chunk bounds.** With more than `PLUTO_VSM_MAX_DRAW_CHUNKS` clustered chunks in a frame, VSM falls back to
  64-instance chunks, and each of those took its whole draw's bound: one chunk of a 4,700-clump foliage cell was
  planned into, and as it swayed dirtied, every page the 36 m cell touched. Fallback chunks now bound their own
  instances: the draw's merged geometry bound under each instance's model, padded by the wind's reach at the chunk's
  largest vertical scale (`InstanceChunkBounds`). This applies to every instanced caster, swaying or not.
- **Shadow sway distance.** `WindComponent` gains **Shadow Sway Distance** (default 30 m;
  `SceneWindSettings::shadowSwayDistance`). VSM chunks beyond it cast their rest pose under the draw's still signature
  (`BasicRenderer` `m_shadowStillSignatures`), so their pages stay cached. The foliage wind check verifies that
  animating casters beyond the distance dirty no pages. Cascades still redraw swaying casters every frame. The test
  has since moved from chunks to pages; see [VSM in dense foliage](vsm-dense-foliage.md#per-page-sway).

On Wind Fields' meadow camera, dirty pages per frame fell from 282 to about 115, and page rendering from 2.1 to
1.35 ms. VSM page updates are budget-limited, so most of the gain shows as shadows that keep up with the sway; the
remaining dirty pages are grass within 30 m, whose sway crosses many 0.27 m pages at the finest level.

### Results (default preset, GPU ms)

| Change | Before | After |
| --- | --- | --- |
| Interleaved cloud trace: `RHI Volumetric Clouds` | 5.4 | 2.7 |
| Chunk bounds and sway distance: `RHI Virtual Shadow Pages` | 2.1 | 1.35 |
| Shadow mask: `RHI Geometry` (including the 3.0 ms mask) | 12.8 | 7.9 |
| **Frame** | **27.7** | **20.1** |

Every preset after both passes (GPU ms, before the fourth pass in brackets): Low 10.6 (15.6), Medium 15.3 (22.0),
default 20.1 (29.6), default with SSAO and SSGI 32.4 (45.7).

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
