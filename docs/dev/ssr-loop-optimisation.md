# SSR loop optimisation

## Change

The specialised half-resolution trace shader processes its 16 GGX rays in
four groups of four, with an unroll hint on the inner loop. Sample generation,
accumulation order, march positions, refinement budget and resolve remain the
same. Full-resolution and missing-stage fallback shaders retain the original
loop. There are no new passes, resources or quality controls.

Full 16-ray unrolling was rejected: its readbacks differed from the original.
Partial unrolling retained exact RGBA8 results in the capture-sized fixture.
The generated GLSL still computes sample directions dynamically; Vulkan gets
the inner-loop unroll hint without a request to specialise the entire lobe.

OpenGL shader normalisation now removes optional `unroll`/`dont_unroll` loop
hints and their required extension. The local driver rejected the extension
in existing opaque shaders before reaching SSR. Loop semantics are unchanged;
OpenGL chooses its own optimisations. SPIR-V retains its hints.

## Measurement

Added `--ssr-capture-performance` to `PlutoGEVulkanRhiTests`: 1690 x 934 output,
845 x 467 trace, 48 steps and five refinements, matching the supplied capture.
This uses the existing synthetic geometry, not the user's captured scene.
The mode warms up for 256 frames and measures 512 observations per resolution.
Existing project/stress benchmark modes retain their shorter timing windows.

The initial 16/32-frame timing window varied substantially, including the
unchanged full-resolution control. Do not use those exploratory runs as a
speedup claim. Longer sequential A/B then B/A runs on the local RTX 4070 SUPER
gave these half-resolution means:

| Run | Original trace | Partial trace | Original resolve | Partial resolve | Original SSR | Partial SSR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A then B | 0.417882 | 0.393498 | 0.091000 | 0.090576 | 0.508890 | 0.484084 |
| B then A | 0.418892 | 0.390420 | 0.078842 | 0.082746 | 0.497746 | 0.473174 |

All values are milliseconds. Across the two runs, trace improved 6.3% and SSR
total 4.9% (about 0.025 ms in this fixture). The unchanged full-resolution
trace ranged from 1.258 to 1.280 ms. This does not establish a corresponding
gain in the user's scene or an OpenGL speedup.

## Reproduction and evidence

Build `PlutoGEEditor`, `PlutoGEVulkanRhiTests` and `PlutoGEOpenGLRhiTests` in
`RelWithDebInfo` using `out/build/msvc-nvidia`. Run sequentially:

```text
PlutoGEVulkanRhiTests --ssr-capture-performance candidate.rgba
ctest --test-dir out/build/msvc-nvidia -C RelWithDebInfo -R "^PlutoGE(Vulkan|OpenGL)(Ssr|Rhi|TemporalMotion)Tests$" --output-on-failure
```

Preserve the original `SSRTrace.fragment.spv` before rebuilding when measuring
this loop change. Swap only that file between sequential benchmark processes;
restore the candidate afterwards. `--reference-stages` measures the combined
fallback shader and includes the cost of losing stage specialisation, so it
is not an isolated baseline for this optimisation.

Evidence is under `out/build/ssr-loop-pass`: baseline shader source/artifacts,
rejected full-unroll source, benchmark logs, snapshots and build/check logs.
`extended-baseline-{1,2}.log` and `extended-partial-{1,2}.log` contain the timing
runs above. The original/candidate capture-sized RGBA8 streams each contain
303,064,320 bytes and share SHA256
`4D3EDF6BC575EA4C8BEDE1E537CB98FF6BBE52AA3ADD8A363CB48998BE45E024`.

The editor and both test executables built successfully. All six checks in the
command above passed, including exact specialised/fallback image comparisons
on both backends. The 1222 x 796 stress fixture (128 steps, eight refinements)
also produced identical original/candidate snapshot hashes:
`F711DAB378E8C82EF658029F74C2156597BEEED31716798AADBCF534991FD788`.
Its short-window timings remain noisy and are not used for a speedup claim.
Final evidence: `build-final.log`, `validation-final.log`, `partial-stress.log`
and `baseline-stress.log`. The candidate SPIR-V was restored after comparisons.

Next actual-scene comparison: restart the rebuilt editor, retain the same view,
1690 x 934 resolution and effect settings, and compare against 2.004 ms trace,
0.330 ms resolve and 2.334 ms SSR in the supplied active capture. Adaptive ray
counts and temporal reconstruction remain separate quality-changing work.
