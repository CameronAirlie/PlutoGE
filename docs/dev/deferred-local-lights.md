# Deferred local lights

Point, spot and rectangle lights for ordinary opaque surfaces are evaluated once per pixel from the G-buffer, after
the geometry pass, instead of inside every surface shader. Step 3 of the
[renderer optimisation roadmap](renderer-optimisation-roadmap-2026-11.md).

## How it works

- **Which draws.** `DeferredLocalLightEligible`: standard materials (no shader graph, so no graph lighting), not
  outline passes, without clear coat, sheen or thin diffuse transmission. The G-buffer holds everything the light
  loop reads for these: normal, albedo, metallic, roughness and occlusion. Every other draw keeps its local lights in
  the forward shader, as transparent surfaces do.
- **Geometry pipelines.** Eligible draws' pipelines (bound, bindless and terrain/impostor variants) are specialised with
  `kSurfaceDeferredLocalLights` (constant 7, `SurfaceFeatures.slang`): the loop is compiled out and the normal's alpha
  marks the pixel (0.25 or 0.75 instead of 0 or 1; values below one half still mean deferred probe irradiance,
  `encodeSurfaceNormalMarks`).
- **The pass.** `DeferredLocalLights` (`src/DeferredLocalLights.*`, `shaders/DeferredLocalLights.slang`) draws a
  full-screen triangle, additively blended into the HDR colour before decals. It is compiled from `BasicLit.slang` and
  calls the same `accumulateLocalLights` the forward shader calls, so lighting, point and spot shadows, contact shadows
  and micro-shadowing are one implementation.
- **Receivers from depth.** Position comes from the coverage depth (`DepthReceiver.slang`, now shared with the
  directional shadow mask). The screen derivatives are the full-screen pass's own fragment-quad `ddx`/`ddy`, as the
  forward shader's are: spot shadows select levels from them, and per-pixel derivatives moved a level transition by a
  pixel, which showed as a column of acne in `CheckShadowFiltering`. Where a quad straddles a silhouette, a sliver or
  the sky, the neighbour search of `DepthReceiver.slang` replaces them (with quad-origin derivatives).
- **Policy.** `DeferredLocalLightPolicy` (`include/PlutoGE/render/DeferredLocalLightPolicy.h`) decides per frame: Vulkan
  with specialisation, no diagnostic view, at least `minimumLocalLights` (1) local lights, and either a layout that
  already writes the G-buffer or `addSurfaceTargets` (on), which promotes compact layouts to `Surface` and
  `SurfaceIndirect` to `SurfaceAllIndirect`. `BasicRenderer::SetDeferredLocalLights` replaces it;
  `PLUTO_DEFERRED_LOCAL_LIGHTS=0` turns it off. `BasicFrameStats::deferredLocalLights` reports it; the GPU scope is
  `RHI Deferred Local Lights`.

Also fixed on the way: `DepthReceiver.slang` no longer steps to a sky neighbour (depth 0). Under an orthographic
projection such a neighbour reconstructs close to the surface and the "nearer neighbour" rule could take it.

## Measured

ThirdPersonProject Settlement (6 local lights), 1280x720, Low preset, `--frozen-render`, Vega 8, two alternating runs
each:

| | Forward | Deferred |
| --- | --- | --- |
| `RHI Geometry` | 12.29 ms | 9.14 ms |
| `RHI Deferred Local Lights` | | 3.08 ms |
| Scene GPU | 16.23 ms | 16.13 ms |

The pass also matches contact shadows: its receivers carry the surface depth in `SV_Position.z`'s place, as the contact
shadow mask is matched to pixels by depth (`CheckContactShadows` caught the first version losing them).

The geometry pass sheds the loop's 3.15 ms, and the pass spends nearly all of it again: here the loop's cost is the
lights and their shadow lookups, not the surface shader's occupancy. Large triangles leave few helper lanes for the
deferred pass to save. The gain should be larger where thin geometry (foliage) receives local lights, and the pass is
the place to add per-tile light culling or light-count-adaptive work later.

A first version reconstructed derivatives per pixel from neighbouring depths (3.24 ms). Replacing them with constant
derivatives cut the pass to 1.81 ms, but only because spot shadows then chose coarser levels: the quad derivatives
keep the forward path's choices, at 3.08 ms.

## Not done

- OpenGL keeps forward local lights (no specialisation constants).
- Materials with clear coat, sheen, transmission or graph lighting stay forward; adding their parameters to the
  G-buffer would let them join.
- No per-tile light lists: the pass uses the same 16x9x24 clusters as the forward loop.
- Variant-equivalence checks (`CheckSurfaceSpecialization`, `CheckRenderOptimizations`) turn the pass off, since it
  needs specialisation and changes the geometry layout; they compare shaders, not lighting paths.

## Checks

`PlutoGEVulkanDeferredLocalLightTests` (`--deferred-local-lights`): six point lights (one shadowed) and a spot light
over a floor and a pillar match the forward loop within the G-buffer normal's precision (mean 0.11/255, p99 1, max 6);
a clear-coat floor keeps forward lighting exactly (not lit twice). The suite's other checks run with the pass active,
including the spot-receiver acne checks.
