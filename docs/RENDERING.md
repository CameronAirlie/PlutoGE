# Rendering support

This page describes the current source, rather than earlier migration plans.
Choose **File > Project Settings > Graphics API** and save the project. The
editor handles backend changes by restarting its graphics host; the standalone
runtime reads the backend from the project manifest. The engine's initial host
configuration defaults to Vulkan, while a manifest without a backend retains
the OpenGL compatibility default. New projects inherit the active editor API.

## Backends and shaders

For automated local ambient lighting and sparse reflection captures, see
[ambient probe grids](ambient-probe-grids.md).
For the sky at night (moon phases, moonlight handoff and stars), see
[night sky rendering](NIGHT_SKY.md).

For clustered local lights, scoped Vulkan synchronisation, bindless GPU-driven
opaque draws and the frame graph with async compute, and their switches, see
[renderer modernisation](renderer-modernisation.md).
SSR and SSAO run as screen-space passes over a shared surface guide; their stages,
costs and rejected alternatives are in
[screen-space effects optimisation](screen-space-effects-optimisation.md). The
reference SSR technique's **Adaptive Rays** setting is described in
[SSR adaptive rays and filtering](ssr-adaptive-rays-and-filter.md).

The render hardware interface (RHI) has Vulkan and OpenGL implementations.
Vulkan scene rendering uses `RhiSceneRenderer`/`BasicRenderer`; the engine also
retains its legacy OpenGL render-pass pipeline. A feature in that legacy pipeline
does not automatically have an RHI implementation.

CMake finds `slangc` and builds `PlutoGERhiShaders`, generating GLSL and SPIR-V
from shared Slang sources. Without the compiler, configure warns and skips
artifact generation; that is not a complete fresh RHI build. Vulkan headers,
Volk and Vulkan Memory Allocator come through the dependency build. A compatible
graphics driver is still required at runtime.

| Area | Current implementation and scope |
| --- | --- |
| Scene rendering | Meshes/instances, LODs, indirect submissions, frustum and occlusion culling, transparent/glass materials, shader graphs, decals, particles, sky/clouds and oceans |
| Animation | Skeletal deformation, including the [Vulkan GPU skinning path](vulkan-skinning.md) |
| Directional shadows | VSM by default in RHI lighting; explicit Cascaded mode remains available |
| Local lights | Point/spot lighting and shadow paths; see [physical local lights](physical-local-lights.md) and [point filtering](point-shadow-filtering.md) |
| RHI post-processing | Tone mapping, gamma, FXAA, color grading, chromatic aberration, bloom, lens flare, motion blur, depth of field, TAA, auto exposure, SSAO (including the LSAO alias), [SSGI](SSGI.md), SSR, volumetric fog, scene composite, VCTGI and [DDGI](DDGI.md) |
| Legacy effects | LPV and RSM still have OpenGL implementations, but no registered RHI post-process adapters |
| Dynamic GI | [DDGI probes](DDGI.md) traced through the voxel scene, and [screen-space GI](SSGI.md); both replace the occluded share of the ambient diffuse light |
| Voxel GI | [World cache](vct-world-cache.md) and [secondary bounce](vct-secondary-bounce.md); requires suitable scene/settings and resource support |
| Water | [Procedural wave model and water effects](OCEAN_WATER_EFFECTS.md), including foam, ripples and caustics; not a fluid simulation |
| Stylized materials | [Toon shaders](ToonShaders.md) and [outlines](OutlineShaders.md) |
| Upscaling | Spatial upscaling; optional Vulkan [FSR 2](FSR2.md) and [DLSS Super Resolution](DLSS.md), subject to build flags, SDKs and hardware |
| UI | Native runtime UI and RmlUi, with OpenGL/Vulkan rendering integrations |

## Environmental lighting and reflection composition

RHI SSAO attenuates diffuse indirect lighting, including light DDGI and SSGI
substitute for it and light VCTGI adds before the AO stage. Direct lighting, emission and environmental
specular remain unchanged. A scene without diffuse indirect lighting can show
occlusion in **AO only** while its lit scene remains unchanged.

The RHI standard and shader-graph surface paths use the same split-sum GGX
response for physical sky and captured reflection lighting. Nonmetals retain
dielectric specular reflections. Diffuse probe grids replace diffuse irradiance
without discarding sky specular; local captures blend diffuse and specular
separately, retaining normalized overlap weights and box-projected reflections.

Physical sky specular uses a packed 64-pixel cubemap with seven GGX roughness
levels and 64 convolution samples. The existing diffuse quadrature occupies the
last atlas row. The atlas regenerates when sky parameters change; unchanged
skies reuse it. Near-mirror surfaces use the analytic sky directly. The reference
sky diagnostic evaluates convolution directly instead of using the atlas.
Sky atlas filtering crosses cube-face edges explicitly. Finite atlas resolution
and convolution samples remain approximations, particularly around narrow
atmospheric features.

SSR retains environmental specular in a separate HDR surface attachment, allocated
when SSR or surface diagnostics require it. Its trace stores a signed lighting
correction, using per-channel hit coverage and the shared integrated material
response. Both the full-resolution path and bilateral reduced-resolution resolve
replace the covered fraction of sky/probe specular. Misses and screen-edge
fades retain the fallback; diffuse light, emission and direct highlights remain.
The trace samples the view-visible GGX normal distribution, keeping ray weights
bounded at grazing angles. The default **Stochastic** technique traces one ray
per half-resolution pixel, re-weights neighbouring rays by each pixel's BRDF (ray
reuse) and accumulates the result in its own reflection history, reprojected
along surface motion or reflection parallax and clipped to the current
neighbourhood; a static view settles within about 16 frames. The **Reference**
technique traces up to 16 rays per pixel and filters the signed correction
horizontally at trace resolution, then vertically while upsampling and
compositing. Its Gaussian footprint grows with roughness (up to 16 trace pixels
per axis); depth-plane, normal and material weights protect surface edges.
Smooth receivers retain bilinear reconstruction. The reference is spatial
filtering with stable samples, independent of TAA, with no reflection history or
motion ghosting. The internal full-resolution comparison path always uses the
reference technique, unfiltered.
SSR strength is bounded to [0,1] and retains the existing roughness fade. Diffuse
AO excludes environmental specular, so AO cannot invalidate the replaceable
fallback. Directional specular occlusion is not supplied by diffuse AO.

These changes apply to Vulkan and OpenGL RHI rendering. The retained legacy
OpenGL lighting-pass implementation is separate. SSR still cannot discover
occluded or off-screen geometry: local reflection captures remain necessary for
those cases. Glass keeps its separate refraction/reflection compositor and uses
the same filtered sky fallback.

## Material texture gradients

Surface fragment entry points compute UV and world-position derivatives before
material branches, alpha discard or lighting. Base colour, normal, metallic,
roughness and emission textures use explicit gradients; depth coverage and glass
boundaries use the same sampling convention. This avoids undefined helper-lane
texture LOD at primitive edges, which can otherwise reveal triangulation as
speckled diagonal lines. Mipmapping and minification filtering remain enabled.

## Directional shadow behavior

New lights and lights without an explicit saved method use VSM. Saved Cascaded
selections remain Cascaded. Active VSM allocates and samples no cascade maps;
missing fine pages use resident coarser VSM coverage. The fixed page pool and
update budgets can produce coarse or delayed shadows while pages warm up.

Unsupported VSM capabilities, missing shaders and vertex-deforming or masked shader graphs report unavailable
directional shadows. Excessive caster draw chunks first cull rigid casters out of shadow reach, then fall back to
cascaded shadow maps for that frame (see virtual-shadow-maps.md).
They do not automatically select cascades. Opaque fragment-only graphs and
standard texture-masked materials are supported. Select Cascaded explicitly for
scenes requiring that compatibility path. See [VSM defaults](VSM_DEFAULT.md) and
[virtual shadow maps](virtual-shadow-maps.md).

## Validation and diagnostics

Use the [Profiler](PROFILER.md), [geometry diagnostics](GEOMETRY_PROFILING.md)
and effect-specific references to inspect the active path and unavailable-feature
reasons. Editor hosts retain rendering diagnostics; standalone hosts disable
OpenGL renderer profiling and Vulkan timestamp/scope profiling automatically.
GPU parent scopes include their child scopes and should not be summed together.

Test the intended backend, optimized configuration and exported build. Existing
regression tests and historical performance captures describe specific workloads;
they do not establish feature parity or performance on every GPU.

Implementation entry points: [render build](../engine/render/CMakeLists.txt),
[post-process adapters](../engine/render/src/RhiPostProcessAdapter.cpp),
[RHI scene renderer](../engine/render/src/RhiSceneRenderer.cpp),
[renderer](../engine/render/src/BasicRenderer.cpp), and
[test registration](../tests/CMakeLists.txt).
