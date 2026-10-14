# RHI glass

Existing `.plutomaterial` assets with `SurfaceType=Glass` now render through the
backend-neutral RHI transparency pass on OpenGL and Vulkan. No asset conversion
is required. Standard materials using `AlphaMode=Blend` also use the pass.
The legacy OpenGL renderer remains available for compatibility.

## Material setup

In the material editor select **Surface Type: Glass**. This selects Blend,
disables shadow casting, sets metallic to zero and transmission to one, and
reduces roughness. Existing values remain editable and retain their file format.

- Base color RGB tints transmission; alpha is surface coverage. Use alpha 1 for
  a complete pane and Transmission 1 for transparent glass. For neutral glass,
  use white base color and attenuation color, and remove colored albedo textures.
  The base color still multiplies the albedo texture; white cannot neutralize a
  cyan texture.
- IOR controls dielectric Fresnel reflection and refraction. 1 disables bending;
  approximately 1.5 is a useful window-glass starting point.
- Thickness is in scene units. It controls a parallel-sided slab's projected
  ray displacement and the absorption path length, including oblique views.
- Attenuation Color is the retained light after traveling Attenuation Distance.
  Absorption follows Beer-Lambert attenuation; white produces no absorption.
- Roughness and the roughness map broaden transmitted scene samples for frost.
  Normal maps perturb reflection and refraction, including Flip Normal Y.
- Two Sided supports thin planes viewed from either side. Closed meshes generally
  work best with it disabled to avoid shading both walls of a slab twice.

The visual reference is Unreal's raster translucent glass workflow, including
[IOR refraction](https://dev.epicgames.com/documentation/unreal-engine/using-refraction-in-unreal-engine)
and its [Thin Translucent material model](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-material-properties).
This implementation is a slab approximation, not Unreal feature parity.

## Rendering behavior and limits

Opaque lighting, GI, ambient occlusion, reflections and atmosphere run first.
Transparent draws are sorted by view-space center, with instances expanded and
sorted individually. Glass reads a stable HDR snapshot containing earlier panes.
It depth-tests against opaque geometry without writing opaque depth, normals,
material data or velocity. It composites with premultiplied coverage before
TAA/upscaling, optics, exposure and tone mapping. Glass and alpha-blended geometry
are excluded from opaque shadow maps and GI voxelization.

Refraction uses screen-space scene color, rejects opaque foreground samples and
fades distortion near screen edges. It cannot retrieve offscreen or hidden
geometry. Reflections use the RHI physical sky environment (ambient fallback)
and local reflection probes, plus the renderer's shadow-tested directional light. The sun disc is excluded
from glass environment reflections to avoid counting unshadowed sunlight twice.
Glass uses exact dielectric Fresnel (including zero reflection at IOR 1).
An active SSR effect also enables transparent glass reflections using the pane's
own world position and shading normal. Glass traces the immutable opaque depth
and HDR color snapshot with the scene's SSR intensity, distance, hit thickness,
start bias, step count, refinement count and edge fade. Smooth glass uses one
ray; rougher panes use four directions around the reflection lobe. Coverage
replaces the sky/probe contribution before Fresnel weighting; misses retain the
environment. Reflections cannot see offscreen geometry or transparent layers.
The snapshot is captured once per transparency pass and shared with scene-reading
shader graphs; transmission retains its ordered per-pane color snapshots.
Camera-to-pane height fog attenuates surface reflection and emission, preserving
in-scattering already present in the transmitted scene. This uses a bounded
per-fragment march at the fog effect's quality; the first fog effect supplies
transparent fog settings. Refraction of fog and local environment occlusion
remain approximations;
colored shadows, caustics and ray-traced refraction are not implemented.
Transparent geometry retains the opaque velocity/depth for temporal processing
and depth of field, so moving panes can ghost. Center sorting cannot correctly
resolve intersecting transparent meshes or triangles within one draw.

Layer correctness currently costs one full-resolution HDR snapshot per
transparent draw, retained in the post-process resource pool. Large numbers of
panes increase bandwidth and memory usage. Frost uses a bounded nine-tap filter,
not a full prefiltered transmission pyramid.

## Validation

`PlutoGEOpenGLRhiTests` and `PlutoGEVulkanRhiTests` run the same glass image checks:
clear transmission, tinted absorption, thickness, coverage, sidedness, opaque
occlusion, G-buffer preservation, layer order, instances, IOR displacement,
frosted transmission, foreground rejection, tone-map ordering and resize.
Glass SSR checks use a transparent floor and colored opaque wall, verify that
SSR preserves the opaque G-buffer, and cover disabling intensity and IOR 1.
The shared shaders are compiled offline to GLSL and SPIR-V.

Validation (14 October 2026): Vulkan and OpenGL `--transparency-only` pass,
including smooth/rough glass SSR, the player's 48-step SSR settings, zero
intensity, index-matched Fresnel and preservation of opaque normal/material
buffers. OpenGL used the existing isolated SSAO depth-shader test override to
remove an unused `GL_EXT_samplerless_texture_functions` requirement; that
startup issue is independent of glass. The `msvc17` Release editor and runtime
were rebuilt together with the shader artifacts.
