#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "SSAOCommon.slang"

[[vk::binding(0, 0)]]
cbuffer Parameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]]
Sampler2D rawAoTexture : register(t1, space0);
[[vk::binding(2, 0)]]
Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]]
Sampler2D normalTexture : register(t3, space0);
[[vk::binding(5, 0)]]
Sampler2D motionTexture : register(t5, space0);
[[vk::binding(6, 0)]]
Sampler2D historyTexture : register(t6, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id: SV_VertexID)
{
    return fullscreenVertex(id);
}

[[vk::binding(7, 0)]] Sampler2D sceneTexture : register(t7, space0);
[[vk::binding(8, 0)]] Sampler2D indirectTexture : register(t8, space0);

struct SsaoResolvedOutput
{
    float4 history : SV_Target0;
    float4 color : SV_Target1;
};
SsaoResolvedOutput ssaoOutput(float2 uv, float ao, float depth, float3 normal)
{
    SsaoResolvedOutput output;
    output.history = float4(ao, depth, encodeOctahedral(normal));
    #ifndef PLUTO_SSAO_FILTER_ONLY
    const float4 scene = sceneTexture.SampleLevel(uv,0);
    const float3 indirect = indirectTexture.SampleLevel(uv,0).rgb;
    output.color=ssaoComposite(scene,indirect,ao,pass.parameters[1].y>0.5);
    #else
    output.color=float4(0);
    #endif
    return output;
}

SsaoResolvedOutput resolveSsao(float2 textureUv)
{
    // Integer addressing makes the exact half-texel boundaries of odd viewports
    // independent of raster interpolation and compute floating-point rounding.
    const int2 pixel = int2(textureUv/pass.inverseResolution);
    textureUv = (float2(pixel)+0.5)*pass.inverseResolution;
    // Every sampled render target uses texture space. Vulkan's negative
    // fullscreen viewport means interpolated vertex UVs must be oriented for
    // raw AO and history just as they are for the geometry buffers.
    const float centerDepth = depthTexture.SampleLevel(textureUv,0).r;
    if (centerDepth <= 0.000001)
        return ssaoOutput(textureUv, 1.0, 0.0, float3(0,0,1));
    const float3 centerWorld = reconstructWorldPosition(textureUv, centerDepth,
                                                        pass.inverseViewProjection, pass.flipY,
                                                        pass.zeroToOneDepth);
    const float3 centerView = mul(pass.view, float4(centerWorld, 1.0)).xyz;
    const float3 centerNormal = decodeNormal(normalTexture.SampleLevel(textureUv,0).xyz);
    const int blurRadius = clamp(int(pass.parameters[1].x), 0, 4);
    const int2 rawSize = int2(round(1.0/pass.parameters[2].zw));
    const int2 outputSize = int2(round(1.0/pass.inverseResolution));
    const int2 rawCenter = ((pixel*2+1)*rawSize)/(outputSize*2);
    const float3 viewNormal = mul((float3x3)pass.view, centerNormal);
    float aoSum = 0.0;
    float weightSum = 0.0;
    float minAo = 1.0;
    float maxAo = 0.0;
    for (int y = -blurRadius; y <= blurRadius; ++y)
    {
        for (int x = -blurRadius; x <= blurRadius; ++x)
        {
            if (blurRadius >= 2 && abs(x) + abs(y) > blurRadius)
                continue;
            const float2 offset = float2(x, y);
            const int2 coordinate=clamp(rawCenter+int2(x,y),int2(0),rawSize-1);
            const float4 sample=rawAoTexture.Load(int3(coordinate,0));
            if (sample.g<=0.0) continue;
            const float2 sampleUv=(float2(coordinate)+0.5)/float2(rawSize);
            const float3 samplePosition=ssaoViewPosition(sampleUv,sample.g,pass);
            const float depthDelta=abs(dot(viewNormal,samplePosition-centerView));
            if (depthDelta>0.35) continue;
            const float3 sampleNormal=decodeOctahedral(sample.ba);
            float normalWeight = max(dot(centerNormal, sampleNormal), 0.0);
            normalWeight *= normalWeight;
            normalWeight *= normalWeight;
            normalWeight *= normalWeight;
            normalWeight *= normalWeight;
            const float weight = (1.0 / (1.0 + dot(offset, offset) * 0.7)) *
                                 (1.0 / (1.0 + depthDelta * 24.0)) * normalWeight;
            const float sampleAo = sample.r;
            aoSum += sampleAo * weight;
            weightSum += weight;
            minAo = min(minAo, sampleAo);
            maxAo = max(maxAo, sampleAo);
        }
    }
    const float currentAo = weightSum > 0.0001 ? aoSum / weightSum
        : (pass.parameters[1].w > 0.5 ? 1.0 : rawAoTexture.SampleLevel(textureUv,0).r);
    if (weightSum <= 0.0001) minAo = maxAo = currentAo;
    float resolvedAo = currentAo;
    if (pass.parameters[5].w > 0.5)
    {
        const float2 orientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
        const float2 motion = motionTexture.SampleLevel(textureUv,0).xy * orientation;
        const float2 historyUv = textureUv - motion;
        if (all(historyUv >= 0.0) && all(historyUv <= 1.0))
        {
            const float4 history = historyTexture.SampleLevel(historyUv,0);
            const float previousViewDepth = -mul(pass.worldToLocal, float4(centerWorld, 1.0)).z;
            const float relativeDepthDelta = abs(history.g - previousViewDepth) / max(previousViewDepth, 0.001);
            const float normalSimilarity = dot(decodeOctahedral(history.ba), centerNormal);
            if (relativeDepthDelta <= pass.parameters[2].x && normalSimilarity >= pass.parameters[2].y)
                resolvedAo = lerp(currentAo, clamp(history.r, minAo, maxAo), pass.parameters[1].z);
        }
    }
    return ssaoOutput(textureUv, resolvedAo, -centerView.z, centerNormal);
}

[shader("fragment")]
#ifdef PLUTO_SSAO_FILTER_ONLY
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    return resolveSsao(orientFullscreenUv(input.uv,pass.flipY)).history;
}
#else
SsaoResolvedOutput fragmentMain(FullscreenVertexOutput input)
{
    return resolveSsao(orientFullscreenUv(input.uv,pass.flipY));
}
#endif
