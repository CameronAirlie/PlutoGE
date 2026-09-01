#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]]
cbuffer Parameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]]
Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]]
Sampler2D depthTexture : register(t2, space0);

float hash31(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
float valueNoise(float3 p)
{
    const float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash31(i), hash31(i + float3(1, 0, 0)), f.x),
                     lerp(hash31(i + float3(0, 1, 0)), hash31(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(hash31(i + float3(0, 0, 1)), hash31(i + float3(1, 0, 1)), f.x),
                     lerp(hash31(i + float3(0, 1, 1)), hash31(i + 1.0), f.x), f.y),
                f.z);
}
float fbm(float3 p)
{
    return valueNoise(p) * 0.55 + valueNoise(p * 2.03 + float3(17.1, 9.2, 13.7)) * 0.275 + valueNoise(p * 4.12 + 9.7) * 0.1375;
}

float densityAt(float3 worldPosition, float4x4 inverseVolume)
{
    const float3 local = mul(inverseVolume, float4(worldPosition, 1.0)).xyz;
    if (any(abs(local) > 0.5))
        return 0.0;
    const float height = local.y + 0.5;
    const float2 edge = 0.5 - abs(local.xz);
    const float shape = smoothstep(0.0, 0.14, height) * (1.0 - smoothstep(0.62, 1.0, height)) *
                        smoothstep(0.0, 0.07, min(edge.x, edge.y));
    const float coverage = max(pass.parameters[0].w, 0.001);
    float density = saturate((fbm((worldPosition + pass.parameters[1].xyz) * pass.parameters[4].w) -
                              (1.0 - coverage)) /
                             coverage);
    if (density > 0.001 && pass.parameters[5].y > 0.001)
        density = saturate(density - (1.0 - valueNoise((worldPosition + pass.parameters[1].xyz) *
                                                           pass.parameters[5].x +
                                                       float3(31.4, 7.8, 19.2))) *
                                         pass.parameters[5].y * (1.0 - density));
    return density * shape * pass.parameters[1].w;
}

float2 intersectVolume(float3 origin, float3 direction, float4x4 inverseVolume)
{
    const float3 localOrigin = mul(inverseVolume, float4(origin, 1.0)).xyz;
    const float3 localDirection = mul(inverseVolume, float4(direction, 0.0)).xyz;
    const float3 safeDirection = sign(localDirection) * max(abs(localDirection), 0.000001);
    const float3 t0 = (-0.5 - localOrigin) / safeDirection;
    const float3 t1 = (0.5 - localOrigin) / safeDirection;
    const float3 low = min(t0, t1), high = max(t0, t1);
    return float2(max(max(low.x, low.y), low.z), min(min(high.x, high.y), high.z));
}

float lightTransmittance(float3 position, float3 lightDirection, float4x4 inverseVolume, uint lightSteps)
{
    const float2 hit = intersectVolume(position + lightDirection * 0.01, lightDirection, inverseVolume);
    const float stepLength = max(hit.y, 0.0) / lightSteps;
    float opticalDepth = 0.0;
    position += lightDirection * stepLength * 0.5;
    for (uint index = 0; index < 16u; ++index)
    {
        if (index >= lightSteps)
            break;
        opticalDepth += densityAt(position, inverseVolume) * pass.parameters[2].w * stepLength;
        position += lightDirection * stepLength;
    }
    return exp(-opticalDepth);
}

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id: SV_VertexID)
{
    return fullscreenVertex(id);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float4x4 inverseVolume = pass.worldToLocal;
    const float3 farPosition = reconstructWorldPosition(uv, 1.0, pass.inverseViewProjection, pass.flipY);
    const float3 rayDirection = normalize(farPosition - pass.cameraPosition.xyz);
    const float2 hit = intersectVolume(pass.cameraPosition.xyz, rayDirection, inverseVolume);
    float end = hit.y;
    const float depth = depthTexture.Sample(uv).r;
    if (depth > 0.000001)
    {
        const float3 opaquePosition = reconstructWorldPosition(uv, depth, pass.inverseViewProjection, pass.flipY);
        end = min(end, max(dot(opaquePosition - pass.cameraPosition.xyz, rayDirection), 0.0));
    }
    const float start = max(hit.x, 0.0);
    if (end <= start)
        return scene;

    const uint primarySteps = clamp(pass.quality & 255u, 1u, 128u);
    const uint lightSteps = clamp((pass.quality >> 8u) & 255u, 1u, 16u);
    const float stepLength = (end - start) / primarySteps;
    const float jitter = 0.5;
    float distanceAlongRay = start + jitter * stepLength;
    float transmittance = 1.0;
    float3 radiance = 0.0;
    const float anisotropy = clamp(pass.parameters[4].y, -0.9, 0.9);
    const float cosine = dot(rayDirection, normalize(pass.parameters[2].xyz));
    const float phase = (1.0 - anisotropy * anisotropy) /
                        (12.56637061 * pow(max(1.0 + anisotropy * anisotropy - 2.0 * anisotropy * cosine, 0.0001), 1.5));
    for (uint index = 0; index < 128u; ++index)
    {
        if (index >= primarySteps || transmittance < 0.01)
            break;
        const float3 samplePosition = pass.cameraPosition.xyz + rayDirection * distanceAlongRay;
        const float density = densityAt(samplePosition, inverseVolume);
        const float segmentTransmittance = exp(-density * pass.parameters[2].w * stepLength);
        const float sunlight = pass.parameters[3].w > 0.0001
                                   ? lightTransmittance(samplePosition, normalize(pass.parameters[2].xyz), inverseVolume, lightSteps)
                                   : 1.0;
        const float3 incident = pass.parameters[4].z +
                                pass.parameters[3].rgb * pass.parameters[3].w * phase * sunlight;
        radiance += transmittance * pass.parameters[0].rgb * incident * pass.parameters[4].x * (1.0 - segmentTransmittance);
        transmittance *= segmentTransmittance;
        distanceAlongRay += stepLength;
    }
    return float4(scene.rgb * transmittance + radiance, scene.a);
}
