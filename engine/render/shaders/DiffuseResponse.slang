// The diffuse response of an opaque surface to ambient light, matching BasicLit's: ambient irradiance (cosine-weighted
// mean radiance) times this is the diffuse light the geometry pass writes to the indirect target. Screen-space passes
// that replace part of that light (DDGIApply, SSGI, DiffuseProbeResolve) use it to stay consistent with the surfaces
// they relight.
#ifndef PLUTO_DIFFUSE_RESPONSE
#define PLUTO_DIFFUSE_RESPONSE

#include "AmbientOcclusion.slang"

// Thin translucency in the G-buffer (docs/FOLIAGE_TRANSMISSION.md). The albedo target holds the reflected share of the
// base colour, and its alpha the far side's response relative to it: 1 for ordinary surfaces (and whatever else writes
// the target, such as decals), lower with stronger transmission. A scale over albedo keeps per-texel colour and needs one
// channel; the geometry pass's own direct and sky light use the exact colour.
static const float FoliageTransmittanceRange = 8.0;
// The indirect target's alpha on translucent surfaces: lit (above one half), and half strength for screen-space AO.
static const float FoliageIndirectAlpha = 0.75;

float encodeFoliageTransmittance(float3 transmittance, float3 albedo)
{
    const float3 luminance = float3(0.2126, 0.7152, 0.0722);
    return 1.0 - saturate(dot(transmittance, luminance) / max(dot(albedo, luminance), 0.02) / FoliageTransmittanceRange);
}

// Transmitted colour for materials following their albedo: leaf cells pass more light than they reflect. Poly Haven's
// measured leaf colours are about this far above their albedos.
float3 foliageAlbedoTint(float3 albedo)
{
    return saturate(albedo * 2.5);
}

// Diffuse reflectance of the far side: `albedo` is the albedo target's texel.
float3 foliageFarAlbedo(float4 albedo)
{
    return albedo.rgb * (1.0 - saturate(albedo.a)) * FoliageTransmittanceRange;
}

float3 ambientDiffuseResponse(float3 albedo, float metallic, float roughness, float3 normal, float3 viewDirection)
{
    const float ndotv = max(saturate(dot(normal, viewDirection)), 0.0001);
    const float3 f0 = lerp(float3(0.04), albedo, metallic);
    const float3 fresnel = f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - ndotv, 5.0);
    return albedo * (1.0 - fresnel) * (1.0 - metallic) / 3.14159265358979;
}

// The ambient response of the surface's far side, to the irradiance arriving there (zero for opaque surfaces).
float3 foliageFarResponse(float4 albedo, float materialOcclusion)
{
    return foliageFarAlbedo(albedo) / 3.14159265358979 * multiBounceOcclusion(saturate(materialOcclusion), albedo.rgb);
}

// The same response occluded by the surface's material ambient occlusion (the material target's alpha), as the
// geometry pass occludes the ambient light it writes.
float3 ambientDiffuseResponse(float3 albedo, float metallic, float roughness, float3 normal, float3 viewDirection,
                              float materialOcclusion)
{
    return ambientDiffuseResponse(albedo, metallic, roughness, normal, viewDirection) *
           multiBounceOcclusion(saturate(materialOcclusion), albedo);
}

#endif
