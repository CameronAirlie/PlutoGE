# DungeonCrawler Radeon performance pass — 2026-09-24

The retained change gives ordinary opaque materials a fragment shader without the
material-graph interpreter. Geometry GPU time decreased **2.8%**, from **13.010 ms
to 12.643 ms**, across three interleaved baseline/candidate pairs. Total scene GPU
observations decreased approximately **1%**. This is a modest improvement; it does
not make the game reach 60 FPS on this machine at native 1080p.

## Workload and measurement

- AMD Ryzen 7 5700G with AMD Radeon(TM) Graphics, Windows, Vulkan.
- Standalone Release runtime and Release managed scripts, 1920×1080, VSync off.
- Original graphics preset, 240 warmup frames and 1,800 measured frames per run.
- Fixed 60 Hz courtyard orbit, all 16 enemies alive, no player attacks.
- The same captured managed assembly, scene and post-process hashes in every run.
- No compilation or other tests during timed runs. Existing user applications
  were left running. Streamline is disabled by the existing benchmark runner.

The early baseline was 40.87–41.34 ms/frame, with approximately 39–40 ms of GPU
work but only 1.34 ms of scene updates. The large GPU scopes were geometry
(approximately 13 ms), VCT GI (10.4 ms), SSAO (4.9 ms) and TAA (3.9 ms).
Scopes overlap, and GPU observations arrive asynchronously; these values must not
be added to their parent scopes or treated as measurements aligned to CPU rows.

Final runs alternate candidate/baseline three times, retaining 5,400 frames per
arm. Frame percentiles below pool the individual frame samples. GPU values average
the per-run means.

| Metric | Baseline | Candidate |
|---|---:|---:|
| Geometry GPU | 13.010 ms | 12.643 ms |
| Scene GPU observations | 38.977 ms | 38.575 ms |
| Mean frame time | 40.851 ms | 39.617 ms |
| Derived FPS | 24.48 | 25.24 |
| Frame p95 | 44.931 ms | 42.232 ms |
| Frame p99 | 46.741 ms | 43.581 ms |
| Median of run mean frame times | 39.983 ms | 39.633 ms |

The final baseline run had additional CPU/presentation overhead: 42.809 ms frame
time despite 39.116 ms scene GPU time. It is included, not discarded. Consequently,
the pooled 3.0% frame-time reduction is less persuasive than the consistent geometry
reduction. Do not generalise it into a guaranteed FPS or tail-latency improvement.

| Arm | Run directory under DungeonCrawler `Tests/BenchmarkRuns` | Frame mean | Geometry GPU |
|---|---|---:|---:|
| Candidate | `20260924-104937-e50e0cbb` | 39.633 ms | 12.623 ms |
| Baseline | `20260924-105206-83de280f` | 39.762 ms | 13.009 ms |
| Candidate | `20260924-105431-3fa1ea2a` | 39.426 ms | 12.647 ms |
| Baseline | `20260924-105651-80d43689` | 39.983 ms | 12.985 ms |
| Candidate | `20260924-105918-611fe76d` | 39.793 ms | 12.659 ms |
| Baseline | `20260924-110154-985e7a22` | 42.809 ms | 13.034 ms |

Every run passed workload validation and exited cleanly. Both arms averaged
60,548.80 skinned vertices, 107.13 geometry draws and 431.13 shadow draws.
Runtime/shader hashes and the aggregation are preserved in
[the results JSON](dungeon-radeon-performance-results.json). Raw frame CSVs and
staged assets remain in the ignored benchmark directories.

## Retained implementation

`BasicLitStandard.slang` compiles the existing material shader with surface and
per-light graph evaluation removed. The renderer selects it only when a draw has
no shader-graph program, is not transparent and is not an outline. Instanced and
non-instanced variants support normal and diagnostic geometry attachments.

Materials with graph programs retain the original shader. Packages without the
optional specialised shader fall back to the original pipeline. Texture sampling,
lighting, shadows, alpha coverage, animation, draw order, scene assets and quality
settings are unchanged. The general fragment SPIR-V binary still matches the
saved baseline. The specialised SPIR-V is 97,188 bytes versus 153,860 bytes for the
general shader; binary size is supporting information, not a GPU register count.
The additional pipeline variants require shader-cache/startup work.

The Release runtime and both Release RHI test executables were rebuilt. The editor
was not rebuilt; rebuild its chosen configuration to use this optimisation there.

## Validation

- Vulkan and OpenGL specialised-material/reference comparisons passed for 18
  material/alpha/instancing cases, with a maximum permitted difference of one
  8-bit output value and mean tolerance of 0.01.
- Vulkan shader-graph rendering passed, verifying the interpreter path remains.
- All ten final Vulkan/OpenGL checks passed: geometry diagnostics, sky quadrature,
  VSM-only rendering, temporal motion and opaque batching.
- The OpenGL legacy graph test failed shader linking with
  `GL_MAX_TEXTURE_IMAGE_UNITS`; the ordinary-material OpenGL comparisons passed.
  The original Debug test executable, last written on 2026-09-23 before this task,
  reproduces the same error. This legacy limitation predates the change.

Logs: `out/build/benchmark-standard-material-build.log`,
`out/build/benchmark-standard-material-tests.log`, and
`out/build/benchmark-standard-final-tests.log`. Baseline failure isolation is in
`out/build/benchmark-original-opengl-graph.log`.

## Rejected experiments

These were measured and removed; none is part of the retained source change.

- Direct SSAO diamond traversal: SSAO remained approximately 4.92 ms.
- Skipping zero-weight sky specular work: geometry remained approximately 13 ms.
- Full-grid 5×5 shadow gathers: image checks passed, but geometry regressed to
  16.44 ms. The strip-based revision was slower still.
- Integer texel loads for GI mip construction: publication remained approximately
  2.21 ms versus 2.25 ms, without a useful whole-frame change.
- Front-to-back opaque ordering: eight rendering checks passed, but geometry
  remained approximately 13.1 ms.

The remaining bottleneck is GPU rendering on this integrated GPU. Larger gains
need further measured changes to shading/GI/post-processing, or explicitly chosen
quality/resolution tradeoffs. This orbit fixture does not validate combat hitches,
editor performance, other scenes or other hardware.

## Reproduction

From the DungeonCrawler checkout, with the Release runtime already built:

```powershell
./Tools/run_benchmark.ps1 `
  -EngineRoot C:/Users/Cameron.Airlie/dev/PlutoGE `
  -Configuration Release -Width 1920 -Height 1080 `
  -Frames 1800 -Warmup 240 -TimeoutSeconds 360
```

The saved pre-change runtime is
`out/build/msvc-nvidia/runtime/Release/PlutoGERuntime.benchmark-baseline.exe` and
its shader package is `out/build/dungeon-benchmark-baseline/shaders` in PlutoGE.
Pass these through `-Runtime` and `-ShaderDirectory` for baseline runs; pass
`-ManagedAssembly` with the captured `DungeonCrawler.Benchmark.dll` to keep managed
code identical. Never compare an old executable against a newly built shader
directory unintentionally.
