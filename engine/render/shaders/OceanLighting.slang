#include "VirtualShadowCommon.slang"
#include "VirtualShadowSampling.slang"
// Prefix of BasicFrameParameters, shared with the opaque lighting pass.
[[vk::binding(7, 0)]] cbuffer SceneLighting : register(b7, space0)
{
    column_major float4x4 viewProjection;
    float4 cameraPositionAmbient;
    float4 directionalDirectionIntensity;
    float4 directionalColor;
    column_major float4x4 shadowMatrices[4];
    uint shadowsEnabled;
    uint shadowFlipY;
    float shadowDepthScale;
    float shadowDepthBias;
    float4 shadowInverseResolutions[4];
    float4 shadowCascadeSplits;
    float4 shadowCascadeMetrics[4];
    float4 shadowCascadeParameters;
    float4 shadowFilterParameters;
    float4 shadowFilterEdgeParameters;
    column_major float4x4 view;
    column_major float4x4 motionViewProjection;
    column_major float4x4 previousViewProjection;
    float4 physicalSkyParameters[6];
    float4 physicalSkySettings;
};
[[vk::binding(8, 0)]] Sampler2D shadowTexture0 : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D shadowTexture1 : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D shadowTexture2 : register(t10, space0);
[[vk::binding(11, 0)]] Sampler2D shadowTexture3 : register(t11, space0);
[[vk::binding(12, 0)]] cbuffer VirtualShadowData : register(b12, space0) { VirtualShadowParameters vsm; };
[[vk::binding(13, 0)]] Sampler2D virtualShadowAtlas : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D virtualShadowTable : register(t14, space0);
static const float PI = 3.14159265359;
#include "PhysicalSkyEnvironment.slang"

float oceanShadowDepth(int cascade, float2 uv)
{
    if (cascade == 0) return shadowTexture0.SampleLevel(uv, 0).r;
    if (cascade == 1) return shadowTexture1.SampleLevel(uv, 0).r;
    if (cascade == 2) return shadowTexture2.SampleLevel(uv, 0).r;
    return shadowTexture3.SampleLevel(uv, 0).r;
}
float oceanCascadeVisibility(int cascade, float3 world, float3 normal)
{
    float worldTexel = max(shadowCascadeMetrics[cascade].x, .00001);
    float slope = 1.0 - saturate(dot(normal, normalize(-directionalDirectionIntensity.xyz)));
    float4 clip = mul(shadowMatrices[cascade], float4(world + normal * worldTexel * (.25 + slope), 1));
    float3 coordinate = clip.xyz / max(abs(clip.w), .000001);
    float2 uv = coordinate.xy * .5 + .5;
    if (shadowFlipY != 0) uv.y = 1.0 - uv.y;
    if (any(uv < 0) || any(uv > 1) || coordinate.z < 0 || coordinate.z > 1) return 1;
    float bias = worldTexel * (.25 + slope) / max(shadowCascadeMetrics[cascade].y, .0001);
    float receiver = coordinate.z * shadowDepthScale + shadowDepthBias - bias * shadowDepthScale;
    float2 texel = shadowInverseResolutions[cascade].xy * max(shadowCascadeParameters.z, 1.0);
    float visibility = 0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibility += receiver <= oceanShadowDepth(cascade, uv + float2(x,y) * texel) ? 1.0 : 0.0;
    return visibility / 9.0;
}
float oceanVisibility(float3 world, float3 normal)
{
    if (shadowsEnabled == 0) return 1;
    if (shadowCascadeParameters.w > .5)
    {
        // Filter in world space so adjacent taps can cross virtual page boundaries.
        float texel = max(vsm.metrics[0].x, .00001);
        float3 receiver = world + normal * texel;
        float visibility = 0;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                visibility += virtualShadowVolumeVisibility(vsm, virtualShadowAtlas, virtualShadowTable,
                    receiver + float3(x,0,y) * texel, 1.0);
        return visibility / 9.0;
    }
    int count = clamp(int(shadowCascadeParameters.x), 1, 4);
    float distance = -mul(view, float4(world,1)).z;
    if (distance > shadowCascadeSplits[count-1]) return 1;
    int cascade = count-1;
    for (int i = 0; i < count; ++i)
        if (distance <= shadowCascadeSplits[i]) { cascade = i; break; }
    float visibility = oceanCascadeVisibility(cascade, world, normal);
    float blendDistance = max(shadowCascadeParameters.y, .0001);
    float blend = saturate((distance - shadowCascadeSplits[cascade] + blendDistance) / blendDistance);
    if (cascade+1 < count && blend > 0)
        visibility = lerp(visibility, oceanCascadeVisibility(cascade+1, world, normal), blend);
    else if (cascade+1 == count)
        visibility = lerp(visibility, 1.0, blend);
    return visibility;
}
float3 oceanAmbient(float3 normal)
{
    return physicalSkySettings.x > .5
        ? samplePhysicalSkyIrradiance(normal) * max(physicalSkySettings.z, 0.0)
        : float3(max(cameraPositionAmbient.w, 0.0));
}
float3 oceanEnvironment(float3 direction, float roughness, float3 ambient)
{
    float3 radiance = physicalSkySettings.x > .5
        ? samplePhysicalSkyEnvironment(direction, 0.0) * max(physicalSkySettings.z, 0.0)
        : float3(max(cameraPositionAmbient.w, 0.0));
    return lerp(radiance, ambient, roughness * roughness);
}
// Cook-Torrance GGX with water's dielectric F0 (IOR approximately 1.333).
float oceanSunSpecular(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    float3 halfVector = viewDirection + lightDirection;
    halfVector /= max(length(halfVector), .0001);
    float nv = max(dot(normal, viewDirection), .0001);
    float nl = saturate(dot(normal, lightDirection));
    float nh = saturate(dot(normal, halfVector));
    float vh = saturate(dot(viewDirection, halfVector));
    float a2 = pow(roughness, 4.0);
    float denominator = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / max(PI * denominator * denominator, .0000001);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float geometry = nv / (nv * (1.0-k) + k) * nl / (nl * (1.0-k) + k);
    float fresnel = .0204 + .9796 * pow(1.0-vh, 5.0);
    return distribution * geometry * fresnel / max(4.0 * nv, .0001);
}
