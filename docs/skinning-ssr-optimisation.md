# CPU skinning and SSR: first pass

Shadow optimisation is closed at the validated SSS gather result. This phase
targets the roughly 4 ms CPU skinning/upload cost and 1.85 ms SSR GPU cost in
the latest project capture, preserving output quality and the attached debugger.

## CPU skinning

Blend GLM matrix columns using baseline SSE2 operations on supported x86
targets. Unaligned loads preserve the existing palette alignment contract.
Each lane retains the original influence order and separate multiply/add;
weight validation and normalization are unchanged. The downstream normal
cofactors, reflected tangent handling, vertex history and fused bounds remain
unchanged. Other architectures keep the scalar implementation. No thread pool,
new allocations or renderer resource changes are introduced.

The same benchmark harness ran against the original scalar kernel and the
candidate in RelWithDebInfo: 73,683 vertices, four influences, reusable in-place
history, eight warmup poses and three runs of 40 poses each.

| Kernel | Run 1 | Run 2 | Run 3 | Mean ms/pose |
| --- | ---: | ---: | ---: | ---: |
| Original scalar | 4.11289 | 4.17657 | 4.00558 | 4.09835 |
| SIMD blend | 3.69673 | 3.64592 | 3.68101 | 3.67455 |

This synthetic four-influence workload improves about 10.3%. It is not a
measurement of the actual SSS weight distribution or an FPS promise. Existing
reference checks cover blended rotations, reflections, nonuniform/singular
scales, invalid joint indices, normalized/unweighted vertices, in-place history,
bounds, independent actors, shared submeshes and in-flight uploads. The benchmark
also compares all four-influence positions, normals and tangents with the
independent reference implementation.

## SSR

Keep the upper crossing endpoint's UV and sampled scene depth during binary
refinement. Reuse them for the final thickness test, eliminating the repeated
projection and depth fetch. The original endpoint remains valid with zero
refinements or an early projection failure. Ray count, march locations, quality,
GGX integration, hit criteria and resolve are unchanged.

At 1222 x 796, all 93,380,352 saved RGBA8 bytes match the original shader.
The benchmark includes smooth/rough/dielectric reflections, metallic tint,
fully rough rejection, and full/half-resolution paths. Both backend SSR tests
also exercise odd and even render extents.

SSR trace timing is effectively unchanged: full-resolution 30.308 -> 30.403 ms,
half-resolution 8.211 -> 8.216 ms in this synthetic stress scene. This removes
redundant work but establishes no speedup. Further SSR work should investigate
conservative hierarchical depth rejection while preserving sample/hit semantics;
it requires a separate design and performance experiment. Do not reduce rays
or trace resolution to claim a fidelity-preserving improvement.

## Validation and follow-up

Builds: `out/build/skinning-ssr-baseline-build.log`, `skinning-simd-build.log`.
Measurements: `skinning-scalar-benchmark.log`, `skinning-ssr-validation.log`,
`ssr-next-before.log`, `ssr-next-after.log` under `out/build`.
Seven initial checks passed: Vulkan skinning plus Vulkan/OpenGL SSR, RHI and
temporal motion. The final focused rerun passed the expanded four-influence
reference check (`out/build/skinning-final-validation.log`). The final editor
build is recorded in `out/build/skinning-final-build.log`.
No scene or effect quality settings changed.

Next project capture should use normal rendering at the same view and resolution.
Compare deformation-plus-bounds separately from vertex upload and SSR trace
separately from resolve. No further shadow-specific capture is needed.
