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

// Not a path but a source: a prepassed pipeline takes the directional shadow from the frame's mask
// (DirectionalShadowMask.slang) instead of filtering it. Off by default, so every other pipeline filters in place.
[vk::constant_id(6)] const bool kSurfaceShadowMask = false;

// Not a path but a destination: the pipeline leaves point, spot and rectangle lights to the deferred local-light pass
// (DeferredLocalLights.slang), which adds them from the G-buffer, and marks its pixels for it. Off by default.
[vk::constant_id(7)] const bool kSurfaceDeferredLocalLights = false;

// The G-buffer normal's alpha: below one half marks deferred probe irradiance (DiffuseProbeResolve.slang), and 0.25 and
// 0.75 mark surfaces whose local lights the deferred pass adds. RGBA8 keeps all four values exact.
float encodeSurfaceNormalMarks(bool probePending, bool deferredLocalLights)
{
    if (probePending)
        return deferredLocalLights ? 0.25 : 0.0;
    return deferredLocalLights ? 0.75 : 1.0;
}

bool surfaceDefersLocalLights(float normalAlpha)
{
    const int mark = int(round(saturate(normalAlpha) * 4.0));
    return mark == 1 || mark == 3;
}

#endif
