# Directional shadow gather pass

## Plan and implementation

1. Retain the scalar virtual-shadow filter as a runtime reference.
2. For a resident 4 x 4 footprint entirely within one page, fetch four 2 x 2
   groups with raw depth gathers. Unroll the comparisons in original row-major
   order. Preserve per-texel receiver-plane correction, bias, tent weights,
   normalization and the nearest diagnostic texel.
3. Keep scalar filtering for partial footprints, other widths and page crossings.
   Residency checks and coarser-level fallback run before either filter path.
4. Compare full images on Vulkan and OpenGL at five softness values with flat
   and tilted receivers. Check both raw and filtered shadow diagnostic output.
5. Benchmark scalar/gather modes in alternating order, with a flat receiver and
   a second scene containing eight overlapping tilted receivers, alternating
   opaque/alpha-tested materials, camera motion and a moving caster.
6. Enable the candidate only if correctness and performance evidence support it.

The existing geometry diagnostic enum gains `ReferenceDirectionalShadows`,
exposed as **Scalar directional shadow filter** in the profiler. This uses the
existing frame diagnostic field; no new buffer layout or GPU resource is needed.
Copied frame metrics now include the softness from that frame's effective
lighting, labelled as configuration rather than a measured fragment tap count.

The fast path is deliberately limited to the common softness-1 footprint.
Gather ordering is explicitly mapped to row-major texels. Coordinates are
exactly representable integer atlas locations divided by the 2048-texel atlas
size, and all four groups remain inside the verified resident physical page.
Raw gathers preserve each tap's independent depth comparison on tilted surfaces.

## Compiler inspection

`out/build/shadow-gather.spvasm` contains four `OpImageGather` instructions.
Its only declared capability is `Shader`; Slang prints an implicit profile
upgrade warning but emits no extended-gather capability for these unoffset
gathers. Runtime rendering checks are still required on both backends.

## Validation artifacts

Build log: `out/build/shadow-gather-build.log`.
The paired benchmark uses the same executable and shader, reverses mode order
on the second run, warms 64 frames and samples the next 32 GPU observations.
Scenario 0 is flat and stationary; scenario 1 has overlapping materials and
motion. Softness values are 0, 0.5, 1 and 4 at 1005 x 594. These synthetic
results cannot establish SSS performance; confirm any enabled path in SSS.

## Results and decision

The RelWithDebInfo editor, Vulkan/OpenGL test executables and profiler tests
built successfully. All 12 selected checks passed: both VSM-only suites,
the paired Vulkan benchmark, profiler capture tests, and Vulkan/OpenGL RHI,
sky quadrature, outline and temporal-motion regressions.

Raw and filtered images are byte-identical to the scalar reference at softness
0, 0.5, 1, 2 and 4 for both flat and tilted receivers, on both backends.
Existing coverage tests for motion, deferred updates, grazing receivers,
coarse fallback under residency pressure, shadow-method switching and fog pass.

Paired geometry GPU timings at 1005 x 594, softness 1:

| Scene | Scalar run 1 / 2 (ms) | Gather run 1 / 2 (ms) | Reduction |
| --- | ---: | ---: | ---: |
| Flat, stationary | 1.82424 / 1.82469 | 1.48503 / 1.48504 | 18.6% |
| Overlapping, tilted, alpha-tested and animated | 5.27845 / 5.27810 | 4.26912 / 4.26887 | 19.1% |

Softness 0, 0.5 and 4 have effectively unchanged timings between runtime modes.
The original diagnostic benchmark's 603 x 346 sky-plus-VSM normal-output case
measures 0.638151 ms geometry, versus approximately 0.76 ms before this pass.
These are synthetic measurements, not a claimed SSS improvement. Native GPU
occupancy/stall counters were not measured.

Decision: enable the gather path in normal rendering. Keep the scalar
comparison mode available for matched project captures and further validation.
Logs: `out/build/shadow-gather-tests.log` and
`out/build/shadow-gather-regressions.log`.

## SSS follow-up

Restart the rebuilt editor with the preferred debugger attached. Use the same
camera and 603 x 346 viewport, select Normal rendering, retain 600 frames and
copy all captured metrics. The report now records actual softness. A subsequent
Scalar directional shadow filter capture can isolate the gather gain within
the same build if the project result is small or ambiguous.

## Closed after SSS validation

The subsequent 240-frame normal-rendering SSS capture confirms softness 1 at
603 x 346, 69 draws and 169,606 triangles. Geometry averages 4.842 ms versus
5.318 ms before gathers (9.0% lower); scene GPU averages 13.430 versus
13.886 ms (3.3% lower). These are separate captures, not a same-build A/B.
The user accepted closing this phase and moving to CPU skinning and SSR.
No further shadow comparison capture is required. Keep the validated gather
path and existing comparison diagnostics available.
