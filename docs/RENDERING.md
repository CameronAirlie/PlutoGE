# Rendering support

This page describes the current source, rather than earlier migration plans.
Choose **File > Project Settings > Graphics API** and save the project. The
editor handles backend changes by restarting its graphics host; the standalone
runtime reads the backend from the project manifest. The engine's initial host
configuration defaults to Vulkan, while a manifest without a backend retains
the OpenGL compatibility default. New projects inherit the active editor API.

## Backends and shaders

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
| RHI post-processing | Tone mapping, gamma, FXAA, color grading, chromatic aberration, bloom, lens flare, motion blur, depth of field, TAA, auto exposure, SSAO (including the LSAO alias), SSGI, SSR, volumetric fog, scene composite and VCTGI |
| Legacy effects | LPV and RSM still have OpenGL implementations, but no registered RHI post-process adapters |
| Voxel GI | [World cache](vct-world-cache.md) and [secondary bounce](vct-secondary-bounce.md); requires suitable scene/settings and resource support |
| Water | [Procedural wave model and water effects](OCEAN_WATER_EFFECTS.md), including foam, ripples and caustics; not a fluid simulation |
| Stylized materials | [Toon shaders](ToonShaders.md) and [outlines](OutlineShaders.md) |
| Upscaling | Spatial upscaling; optional Vulkan [FSR 2](FSR2.md) and [DLSS Super Resolution](DLSS.md), subject to build flags, SDKs and hardware |
| UI | Native runtime UI and RmlUi, with OpenGL/Vulkan rendering integrations |

## Directional shadow behavior

New lights and lights without an explicit saved method use VSM. Saved Cascaded
selections remain Cascaded. Active VSM allocates and samples no cascade maps;
missing fine pages use resident coarser VSM coverage. The fixed page pool and
update budgets can produce coarse or delayed shadows while pages warm up.

Unsupported VSM capabilities, missing shaders, excessive draw chunks, and
vertex-deforming or masked shader graphs report unavailable directional shadows.
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
