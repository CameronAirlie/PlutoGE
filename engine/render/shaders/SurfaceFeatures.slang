// Lighting paths a geometry pipeline can compile out (render/src/SurfaceFeatures.h). Every path also checks its own
// frame or material data, so the defaults (every path present) are the general shader: OpenGL and unspecialised
// pipelines use it. Vulkan geometry pipelines specialise these to the frame and material, and the driver then removes
// the absent paths with their registers; one uber shader sized for the worst case runs few waves per SIMD.
// A constant may only be false where its data would skip the path anyway, so a specialised pipeline draws the same image.
#ifndef PLUTO_SURFACE_FEATURES_SLANG
#define PLUTO_SURFACE_FEATURES_SLANG

// Point, spot and rectangle lights (pointParameters.x > 0).
[vk::constant_id(0)] const bool kSurfaceLocalLights = true;
// Reflection captures (any iblSettings[i].x set).
[vk::constant_id(1)] const bool kSurfaceReflectionCaptures = true;
// Baked diffuse probe volumes (probeOrigin.w > 0).
[vk::constant_id(2)] const bool kSurfaceDiffuseVolumes = true;
// Clear coat and sheen (clearCoatParameters.x > 0 or sheenParameters.rgb not black).
[vk::constant_id(3)] const bool kSurfaceLayers = true;
// Directional receivers: virtual shadow maps (shadowCascadeParameters.w set) and cascades, each with shadowsEnabled.
[vk::constant_id(4)] const bool kSurfaceVirtualShadows = true;
[vk::constant_id(5)] const bool kSurfaceCascadedShadows = true;
// Lightmap sampling (bakedLighting.x set and the draw's lightmap coordinate marked lightmapped). Ids 6 to 8 are the
// constants below; render/src/SurfaceFeatures.h SurfaceFeatureConstantIds maps features to ids.
[vk::constant_id(9)] const bool kSurfaceLightmap = true;

// Not a path but a source: a prepassed pipeline takes the directional shadow from the frame's mask
// (DirectionalShadowMask.slang) instead of filtering it. Off by default, so every other pipeline filters in place.
[vk::constant_id(6)] const bool kSurfaceShadowMask = false;

// Not a path but a destination: the pipeline leaves point, spot and rectangle lights to the deferred local-light pass
// (DeferredLocalLights.slang), which adds them from the G-buffer, and marks its pixels for it. Off by default.
[vk::constant_id(7)] const bool kSurfaceDeferredLocalLights = false;

// Not a path but a guarantee: the pipeline shades a depth prepass's surfaces with an equal depth test, and the prepass
// applied the same alpha test (SurfaceCoverage.slang), so a masked fragment that fails it never passes depth and the
// shading pass need not discard. Without the discard the driver tests depth before shading, so the thin, overlapping
// cards and blades of foliage no longer shade every fragment they rasterize. Off by default (depth passes and every
// pipeline without a prepass keep their discard).
[vk::constant_id(8)] const bool kSurfacePrepassedCoverage = false;

#include "SurfaceNormalMarks.slang"

#endif
