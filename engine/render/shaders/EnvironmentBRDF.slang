// Shared split-sum response for filtered sky and captured environments, and the GGX directional albedo it implies.
// Every pass that writes, replaces or occludes environment reflections (BasicLit, DDGIApply, SSR) uses these, so a
// change here keeps them consistent.
#ifndef PLUTO_ENVIRONMENT_BRDF
#define PLUTO_ENVIRONMENT_BRDF

// Split-sum GGX environment BRDF as a scale (x) and bias (y) of F0, paired with the GGX capture convolution
// (Karis, "Physically Based Shading on Mobile"). x + y is the single-scattering directional albedo of a white metal.
float2 environmentBrdfScaleBias(float roughness, float ndotv)
{
    const float4 r = roughness * float4(-1, -0.0275, -0.572, 0.022) +
                                float4(1, 0.0425, 1.04, -0.04);
    const float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
    return max(float2(-1.04, 1.04) * a004 + r.zw, 0.0);
}

// Single-scattering microfacet models lose the light that bounces between microfacets: up to about half of it on
// rough metals, which then look dull and dark. The lost share of a white surface is 1 - E, E its directional albedo;
// a coloured one keeps part of every further bounce. Fdez-Aguera, "A Multiple-Scattering Microfacet Model for
// Real-Time Image-based Lighting" (2019), with the average Fresnel of Schlick's approximation. The albedo is the
// split-sum fit's own, so a white metal reflects exactly the environment it sees, whatever that fit's error.
float3 multipleScatteringResponse(float3 f0, float2 scaleBias)
{
    const float3 single = f0 * scaleBias.x + scaleBias.y;
    const float albedo = saturate(scaleBias.x + scaleBias.y);
    const float3 averageFresnel = f0 + (1.0 - f0) / 21.0;
    const float3 multiple = single * averageFresnel / (1.0 - (1.0 - albedo) * averageFresnel);
    return single + multiple * (1.0 - albedo);
}

// The environment's specular response: what reflected radiance is multiplied by.
float3 iblSpecularResponse(float3 f0, float roughness, float ndotv)
{
    return multipleScatteringResponse(f0, environmentBrdfScaleBias(roughness, ndotv));
}

// The directional albedo of a white GGX metal (height-correlated Smith masking): a least-squares cubic in roughness
// and sqrt(n.v) fitted to numerically integrated values, within 0.04 of them for n.v >= 0.1. The split-sum fit above
// is not used for this: it underestimates albedo by up to 0.14 at low roughness, which would brighten smooth metals.
float specularDirectionalAlbedo(float roughness, float ndotv)
{
    const float s = sqrt(saturate(ndotv));
    const float4 powers = float4(1.0, s, s * s, s * s * s);
    const float3 lost = float3(dot(float4(0.68201, 0.09221, -4.10235, 3.05989), powers),
                               dot(float4(-2.6064, 6.36997, 2.40939, -5.22765), powers),
                               dot(float4(1.9292, -5.79848, 2.06278, 1.85913), powers));
    return saturate(1.0 - roughness * (lost.x + roughness * (lost.y + roughness * lost.z)));
}

// The factor that restores multiple scattering to analytic lights' single-scattering specular (Kulla and Conty,
// "Revisiting Physically Based Shading at Imageworks", in Filament's form): 1 + F0 (1 / E - 1).
float3 specularEnergyCompensation(float3 f0, float roughness, float ndotv)
{
    return 1.0 + f0 * (1.0 / max(specularDirectionalAlbedo(roughness, ndotv), 0.25) - 1.0);
}

#endif
