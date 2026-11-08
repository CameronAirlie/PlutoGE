# GPU optimisation pass

The supplied editor capture reports 68.22 ms average frame time, with 65.94 ms
on the scene GPU. SSR accounts for 25.31 ms and geometry for 16.24 ms. The
63.60 ms presentation fence wait reflects the outstanding GPU workload; it is
not independent CPU work that can be removed to recover another 63.60 ms.

## Changes

- The RHI SSR path traces indirect specular at ceil(width / 2) by
  ceil(height / 2), retaining all 16 GGX samples and the configured march and
  refinement counts. A full-resolution four-tap resolve weights neighbours by
  surface-plane distance, normal agreement, roughness and metallic value.
- Trace samples select actual full-resolution G-buffer pixels. The original
  scene colour and alpha remain full resolution. Trace/resolve targets use the
  existing reusable post-process pool and resize with the viewport.
- SSR uses explicit level-zero reads for screen buffers, including reads in
  divergent ray-march loops. Zero-intensity SSR skips its draws.
- Virtual shadow filtering reuses page-table entries when the filter footprint
  stays within a page along either axis. Filtering taps and comparison weights
  are unchanged.

Half-resolution tracing trades some fine reflection detail for fewer rays;
depth and normal weighting cannot recover subpixel reflectors that were never
traced. It does not disable rough or dielectric reflections. For comparisons,
`BasicPostProcessEffect::parameters[4].w = 1` selects full-resolution SSR.
Lane 4.xyz is reserved for internal trace/resolve parameters.

## Validation

Build `PlutoGEVulkanRhiTests` and `PlutoGEOpenGLRhiTests`, then run:

```powershell
ctest --test-dir out/build/gcc-profile -R 'PlutoGE(OpenGLSsr|VulkanSsr|VulkanRhi|VctWorldCacheRendering)Tests' --output-on-failure
out/build/gcc-profile/tests/PlutoGEVulkanRhiTests.exe --ssr-performance
```

The SSR checks cover smooth, fully rough and dielectric reflections, metallic
tint, additive lighting, and rough reflection energy relative to the
full-resolution reference. Vulkan also checks odd dimensions and resizing.
The performance command compares both SSR paths at 1222 x 796, discarding
16 warm-up frames and averaging 32 GPU timing samples per path.

On the local AMD Radeon(TM) Graphics device, the optimised profiling build
measured 19.63 ms for full-resolution SSR and 6.24 ms for trace plus resolve
(68% lower). All four tests in the command above passed.

During this first pass, the broader `PlutoGEOpenGLRhiTests` suite failed its shadow-edge plateau
check (70 pixels). Running `--shadows-only` with the unchanged HEAD BasicLit
fragment shader reproduces the same failure; the optimised artifact was
restored afterwards. The dedicated OpenGL SSR check passes. The subsequent
[VSM-only pass](vsm-exclusive-shadow-path.md) corrected the test's missing
debug-output shader and the backend's VSM depth comparisons; shadow checks now pass.

The benchmark is a synthetic reflective floor/wall scene. Reprofile the original
editor scene to measure its total frame-time improvement; geometry, GI, SSAO
and fog still contribute to its GPU budget.
