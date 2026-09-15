# SSR stage optimisation

## Scope

The latest SSS capture reports 1.629 ms tracing and 0.190 ms resolving SSR
at 603 x 346, with 48 march steps, five refinements and 16 GGX rays.
Those settings remain unchanged. CPU skinning and shadows are outside this pass.

## Hierarchy experiment

A separate experimental shader checked eight original march samples against
three maximum-depth levels (8, 32 and 128 pixel blocks). Reductions used R32Float,
included the bilinear footprint and clamped odd-size edge pixels. The experiment
also retained the original first-crossing test and refinement bracket.

It was rejected: in the synthetic project-settings workload, half-resolution
SSR including hierarchy construction increased from 0.858 to 1.159 ms. In the
1222 x 796, 128-step stress workload it increased from 8.769 to 11.189 ms.
Some Vulkan comparisons also differed in a small number of output channels;
the cause was not resolved because the timing results already rejected the path.
No hierarchy resources, runtime branches or shaders remain in production.

Local experiment evidence is archived under `out/build`:
`ssr-hierarchy-rejected.patch`, `SsrDepthHierarchy-rejected.slang`,
`SsrHierarchical-rejected.slang`, `ssr-hierarchy-project.log` and
`ssr-hierarchy-stress.log`.

## Stage specialisation

`SSRTrace.slang` and `SSRResolve.slang` compile the shared `SSR.slang` source
with a fixed stage. The compiler can remove the other stage's code and its
uniform branches. The full-resolution reference keeps the original shader.
Optional stage shader packages fall back to the original shader when absent.
Pipeline ownership and shutdown follow the renderer's existing RAII lifecycle.
There are no additional render passes, targets, samples or quality settings.

The regression checks compare all RGBA8 readbacks against a renderer using
the original package. They exercise roughness, metallic tint, fully rough
receivers, full/half resolution and zero, one, five and eight refinements.
Vulkan also exercises odd dimensions and viewport resizing.

## Reproduction

Build `PlutoGEEditor`, `PlutoGEVulkanRhiTests` and `PlutoGEOpenGLRhiTests` in
`RelWithDebInfo` using `out/build/msvc-nvidia`. Run the Vulkan/OpenGL SSR tests.
The Vulkan rendering test executable accepts:

```text
--ssr-project-performance snapshots.rgba
--ssr-project-performance reference.rgba --reference-stages
```

Vulkan additionally accepts `--ssr-performance` for the larger stress workload.
OpenGL accepts `--ssr-project-snapshots` with the same snapshot filename and
optional `--reference-stages` arguments. Its RHI does not expose GPU timing
results, so this command checks images without attempting a GPU benchmark.
Run comparisons sequentially to avoid competing for the GPU. Each timing uses
16 warmup frames followed by 32 measured frames. The SSR parent includes its
trace and resolve children; do not add the parent to its children.
The benchmark matches SSS settings, but its geometry is synthetic. Actual SSS
gains require a subsequent capture from the same view with the debugger attached.

## Results

Two sequential Vulkan runs, reversing candidate/reference order in the second
run, produced the following means. These are half-resolution SSR GPU times,
including resolve; the full-resolution reference remained within run noise.

| Synthetic workload | Original trace | Specialised trace | Original resolve | Specialised resolve | Original SSR total | Specialised SSR total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 603 x 346, 48 steps, 5 refinements | 0.736 ms | 0.640 ms | 0.118 ms | 0.095 ms | 0.854 ms | 0.736 ms |
| 1222 x 796, 128 steps, 8 refinements | 8.226 ms | 6.805 ms | 0.530 ms | 0.406 ms | 8.756 ms | 7.212 ms |

Total SSR cost improved by approximately 14% and 18%, respectively. Retain the
specialised stages as the normal half-resolution path. These figures establish
a synthetic Vulkan improvement, not an SSS FPS gain or an OpenGL speedup.

All saved RGBA8 readbacks matched the original package exactly: 40,058,496 bytes
for Vulkan project settings, 186,760,704 bytes for Vulkan stress settings and
40,058,496 bytes for OpenGL project settings. These checks cover the fixture;
they do not prove every possible scene produces identical pixels.

The editor and both rendering test executables built successfully. All six
Vulkan/OpenGL SSR, RHI and temporal-motion checks passed. Evidence:
`out/build/ssr-stages-build.log`, `ssr-stages-validation.log`,
`ssr-stages-project*.log`, `ssr-stages-stress*.log`, and the corresponding
reference/specialised `.rgba` snapshots.

Next: restart the rebuilt editor with the debugger attached, keep the same
SSS camera and effect settings, and copy another retained multi-frame capture.
Compare `RHI SSR / Trace`, `RHI SSR / Resolve` and GPU scene time against the
latest 1.629 / 0.190 / 13.093 ms capture. No quality controls need changing.
