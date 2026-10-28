// The diffuse response of an opaque surface to ambient light, matching BasicLit's: ambient irradiance (cosine-weighted
// mean radiance) times this is the diffuse light the geometry pass writes to the indirect target. Screen-space passes
// that replace part of that light (DDGIApply, SSGI) use it to stay consistent with the surfaces they relight.
#ifndef PLUTO_DIFFUSE_RESPONSE
#define PLUTO_DIFFUSE_RESPONSE

float3 ambientDiffuseResponse(float3 albedo, float metallic, float roughness, float3 normal, float3 viewDirection)
{
    const float ndotv = max(saturate(dot(normal, viewDirection)), 0.0001);
    const float3 f0 = lerp(float3(0.04), albedo, metallic);
    const float3 fresnel = f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - ndotv, 5.0);
    return albedo * (1.0 - fresnel) * (1.0 - metallic) / 3.14159265358979;
}

#endif
