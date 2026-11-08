# Directional shadow sampling optimisation

## Evidence

The two SSS captures contain 600 frames each at 603 x 346, with 69 geometry
draws and 169,606 submitted triangles. Both have the debugger attached.
Normal rendering averages 5.355 ms geometry GPU and 13.945 ms scene GPU.
Bypassing directional receiver shadow sampling averages 3.266 ms and
12.015 ms respectively. Shadow receiver depth, planning and page generation
remain approximately 2.36 ms combined. GPU observations are asynchronous.
The 2.089 ms geometry difference identifies sampling cost; it is not a
prediction of recoverable time while preserving fidelity.

## Implementation

`BasicLit.slang::evaluateVirtualShadow` resolves each resident physical page
into an integer virtual-to-atlas offset once per filter footprint. Previously
each tap selected a mapping, decoded its atlas tile and calculated its local
page coordinate. Each filter row now selects its two possible offsets once;
each tap selects left or right and adds the offset to its virtual texel.
Vertical tent weights and the nearest diagnostic texel are also computed
outside the inner loop.

The maximum support is five texels, smaller than the 128-texel page size, so
the footprint still requires at most four mappings. Missing pages retain
the same coarser-level fallback. Integer offsets preserve exact atlas texel
centres even across nonadjacent physical pages. Sample count, accumulation
order, tent weights, receiver-plane correction, bias and softness remain
unchanged. This modifies the virtual directional surface path without adding
renderer state, resource bindings or quality settings.

## Validation procedure

`VsmOnlyRenderingChecks.h` adds a smaller offset occluder with softness
0, 0.5, 1, 2 and 4. Each case requires lit and occluded pixels and, for
softness >= 1, intermediate penumbra pixels. Full-image fingerprints are
logged for comparison against the original shader on each backend.
Existing checks cover movement, deferred refreshes, oblique/grazing receivers,
residency pressure, switching shadow methods and fog coverage.

Reference logs are in `out/build/directional-shadow-before.log` (Vulkan
benchmark) and `out/build/directional-shadow-reference-images.log` (original
Vulkan/OpenGL filter images). Synthetic timings must not be presented as SSS
project improvements. Verify the project with a new normal-rendering capture
from the same view and viewport size after rebuilding.

## Results (2026-09-15)

The RelWithDebInfo editor and both backend test executables built successfully.
All 11 selected rendering checks passed: Vulkan/OpenGL RHI, VSM-only,
sky quadrature, outlines and temporal motion, plus the Vulkan VSM benchmark.
The five full-image fingerprints match the original shader on both backends.
The benchmark was repeated because the measured changes are small.

| Geometry GPU case | Original (ms) | Optimised (ms) | Repeat (ms) |
| --- | ---: | ---: | ---: |
| 603 x 346 sky + VSM, normal output | 0.791518 | 0.761297 | 0.764357 |
| 1005 x 594 sky + VSM, normal output | 1.902040 | 1.833540 | 1.839180 |
| 1005 x 594 VSM softness 1 | 1.516310 | 1.465230 | 1.472870 |
| 1005 x 594 VSM softness 0 | 0.968100 | 0.999531 | 0.999516 |

Softness 1 improves modestly in this synthetic workload (about 3–4% for sky
plus VSM geometry). The smallest filter, softness 0, is approximately 0.03 ms
slower at the larger extent: preparing offsets has a cost that fewer taps
cannot amortise. This pass targets the more expensive soft-shadow path and
does not establish a win for every configuration. No SSS FPS gain is claimed.
Results are in `out/build/directional-shadow-after.log` and
`out/build/directional-shadow-repeat.log`.
