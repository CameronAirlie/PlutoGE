// Shared split-sum response for filtered sky and captured environments.
#ifndef PLUTO_ENVIRONMENT_BRDF
#define PLUTO_ENVIRONMENT_BRDF

float3 iblSpecularResponse(float3 f0, float roughness, float ndotv)
{
    // Split-sum GGX environment BRDF, paired with the GGX capture convolution.
    const float4 r = roughness * float4(-1, -0.0275, -0.572, 0.022) +
                                float4(1, 0.0425, 1.04, -0.04);
    const float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return max(f0 * ab.x + ab.y, 0.0);
}

#endif
