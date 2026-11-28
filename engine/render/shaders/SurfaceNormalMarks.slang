// The G-buffer normal's alpha: what deferred passes add to a pixel. Written by BasicLit.slang, read by the probe grid's
// screen-space resolve (DiffuseProbeResolve.slang) and the deferred local lights (DeferredLocalLights.slang).
#ifndef PLUTO_SURFACE_NORMAL_MARKS
#define PLUTO_SURFACE_NORMAL_MARKS

// What a pixel leaves to the probe grid's screen-space resolve (DiffuseProbeResolve.slang).
static const int SurfaceProbeNone = 0;
// The grid's irradiance and its reflection terms (sky occlusion, capture relighting).
static const int SurfaceProbeIrradiance = 1;
// Its reflection terms only: lightmapped surfaces, whose diffuse light comes from their texels.
static const int SurfaceProbeReflections = 2;

// The G-buffer normal's alpha marks what deferred passes add to a pixel: the probe grid (above) and, where the deferred
// pass adds them, local lights (DeferredLocalLights.slang). Codes in eighths, rounded on decode, so RGBA8 keeps them:
// irradiance 0 (0.25 with local lights), reflections 0.5 (0.375), none 1 (0.75).
float encodeSurfaceNormalMarks(int probe, bool deferredLocalLights)
{
    if (probe == SurfaceProbeIrradiance)
        return deferredLocalLights ? 0.25 : 0.0;
    if (probe == SurfaceProbeReflections)
        return deferredLocalLights ? 0.375 : 0.5;
    return deferredLocalLights ? 0.75 : 1.0;
}

int surfaceProbeMark(float normalAlpha)
{
    const int code = int(round(saturate(normalAlpha) * 8.0));
    return code <= 2 ? SurfaceProbeIrradiance : code <= 4 ? SurfaceProbeReflections : SurfaceProbeNone;
}

bool surfaceDefersLocalLights(float normalAlpha)
{
    const int code = int(round(saturate(normalAlpha) * 8.0));
    return code == 2 || code == 3 || code == 6;
}
#endif
