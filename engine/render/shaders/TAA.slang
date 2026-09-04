#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]]
cbuffer Parameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]]
Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]]
Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]]
Sampler2D normalTexture : register(t3, space0);
[[vk::binding(5, 0)]]
Sampler2D motionTexture : register(t5, space0);
[[vk::binding(6, 0)]]
Sampler2D historyTexture : register(t6, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

float luma(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

float3 tonemap(float3 color)
{
    return color / (max(max(color.r, color.g), color.b) + 1.0);
}

float3 inverseTonemap(float3 color)
{
    return color / max(1.0 - clamp(max(max(color.r, color.g), color.b), 0.0, 0.999), 0.001);
}

float3 clipHistoryToNeighborhood(float3 historyColor, float3 minColor, float3 maxColor)
{
    const float3 center = 0.5 * (maxColor + minColor);
    const float3 extent = 0.5 * (maxColor - minColor) + 0.0001;
    const float3 offset = historyColor - center;
    const float3 unit = offset / extent;
    const float maxUnit = max(max(abs(unit.x), abs(unit.y)), abs(unit.z));
    return maxUnit > 1.0 ? center + offset / maxUnit : historyColor;
}

float4 sampleCatmullRom(Sampler2D source, float2 uv)
{
    uint width = 0;
    uint height = 0;
    source.GetDimensions(width, height);
    const float2 size = float2(width, height);
    const float2 samplePosition = uv * size - 0.5;
    const float2 texel = floor(samplePosition);
    const float2 f = samplePosition - texel;
    const float2 f2 = f * f;
    const float2 f3 = f2 * f;
    const float2 w0 = f2 - 0.5 * (f3 + f);
    const float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    const float2 w3 = 0.5 * (f3 - f2);
    const float2 w2 = 1.0 - w0 - w1 - w3;
    const float2 w12 = w1 + w2;
    const float2 offset12 = w2 / max(w12, float2(0.0001));
    const float2 inverseSize = 1.0 / size;
    const float2 uv0 = (texel - 1.0) * inverseSize;
    const float2 uv12 = (texel + offset12) * inverseSize;
    const float2 uv3 = (texel + 2.0) * inverseSize;

    float4 result = 0.0;
    result += source.Sample(float2(uv0.x, uv0.y)) * w0.x * w0.y;
    result += source.Sample(float2(uv12.x, uv0.y)) * w12.x * w0.y;
    result += source.Sample(float2(uv3.x, uv0.y)) * w3.x * w0.y;
    result += source.Sample(float2(uv0.x, uv12.y)) * w0.x * w12.y;
    result += source.Sample(float2(uv12.x, uv12.y)) * w12.x * w12.y;
    result += source.Sample(float2(uv3.x, uv12.y)) * w3.x * w12.y;
    result += source.Sample(float2(uv0.x, uv3.y)) * w0.x * w3.y;
    result += source.Sample(float2(uv12.x, uv3.y)) * w12.x * w3.y;
    result += source.Sample(float2(uv3.x, uv3.y)) * w3.x * w3.y;
    return result;
}

float2 responsiveMotion(float2 uv)
{
    float closestDepth = depthTexture.Sample(uv).r;
    float2 bestMotion = motionTexture.Sample(uv).xy;
    const float2 offsets[4] = {
        float2(-1.0, 0.0), float2(1.0, 0.0),
        float2(0.0, -1.0), float2(0.0, 1.0)
    };
    for (uint index = 0; index < 4; ++index)
    {
        const float2 sampleUv = saturate(uv + offsets[index] * pass.inverseResolution);
        const float sampleDepth = depthTexture.Sample(sampleUv).r;
        // Reverse-Z: the largest depth belongs to the nearest surface. Dilate
        // its velocity by one pixel so rasterized silhouette gaps do not pick
        // the background velocity and lose temporal coverage.
        if (sampleDepth > closestDepth)
        {
            closestDepth = sampleDepth;
            bestMotion = motionTexture.Sample(sampleUv).xy;
        }
    }
    return bestMotion;
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    if (pass.parameters[3].x > 0.5)
    {
        // Show the raw raster result. RhiSceneRenderer exaggerates projection
        // jitter to four pixels while this diagnostic is active.
        float3 raw = sceneTexture.Sample(uv).rgb;
        // A magenta top/left marker makes it impossible to confuse this with
        // an ordinary history-free TAA frame.
        if (uv.x < 0.012 || uv.y < 0.018)
            raw = lerp(raw, float3(1.0, 0.0, 1.0), 0.8);
        return float4(raw, depthTexture.Sample(uv).r);
    }
    const float2 textureOrientation = float2(1.0, pass.flipY != 0 ? -1.0 : 1.0);
    const float2 currentJitterUv = pass.parameters[2].xy * textureOrientation;
    const float2 previousJitterUv = pass.parameters[2].zw * textureOrientation;
    // A jittered raster sample still belongs to this output pixel; jitter
    // changes its sub-pixel sample location, not its texture address. Shifting
    // the color buffer back here bilinearly filters every pixel on every frame
    // and produces the characteristic stationary whole-frame blur.
    const float2 currentUv = uv;
    const float4 current = sceneTexture.Sample(currentUv);
    const float currentDepth = depthTexture.Sample(currentUv).r;
    if (pass.parameters[5].w < 0.5)
        return float4(current.rgb, currentDepth);

    const float2 encodedMotion = responsiveMotion(currentUv) * textureOrientation;
    // Motion vectors are generated from unjittered matrices and history is
    // stored on the stable output grid, so only geometric motion belongs in
    // history reprojection. Different jitter phases accumulate at the same
    // output texel to provide temporal supersampling.
    const float2 motion = encodedMotion;
    const float2 historyUv = uv - motion;
    if (any(historyUv < 0.0) || any(historyUv > 1.0))
        return float4(current.rgb, currentDepth);

    float3 minColor = 1.0e20;
    float3 maxColor = -1.0e20;
    float3 minHdrColor = 1.0e20;
    float3 maxHdrColor = -1.0e20;
    float3 moment1 = 0.0;
    float3 moment2 = 0.0;
    float3 spatial = 0.0;
    float spatialWeight = 0.0;
    const float centerLuma = luma(current.rgb);
    float minLuma = 1.0e20;
    float maxLuma = -1.0e20;
    float minDepth = currentDepth;
    float maxDepth = currentDepth;
    const float3 centerNormal = normalize(normalTexture.Sample(currentUv).rgb * 2.0 - 1.0);
    float minNormalAgreement = 1.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = saturate(currentUv + float2(x, y) * pass.inverseResolution);
            const float3 sampleColor = sceneTexture.Sample(sampleUv).rgb;
            const float3 mapped = tonemap(sampleColor);
            minColor = min(minColor, mapped);
            maxColor = max(maxColor, mapped);
            minHdrColor = min(minHdrColor, sampleColor);
            maxHdrColor = max(maxHdrColor, sampleColor);
            moment1 += mapped;
            moment2 += mapped * mapped;
            const float sampleLuma = luma(sampleColor);
            minLuma = min(minLuma, sampleLuma);
            maxLuma = max(maxLuma, sampleLuma);
            const float rangeWeight = exp(-abs(sampleLuma - centerLuma) * 4.0);
            const float kernelWeight = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
            const float weight = kernelWeight * rangeWeight;
            spatial += sampleColor * weight;
            spatialWeight += weight;
            const float sampleDepth = depthTexture.Sample(sampleUv).r;
            minDepth = min(minDepth, sampleDepth);
            maxDepth = max(maxDepth, sampleDepth);
            const float3 sampleNormal = normalize(normalTexture.Sample(sampleUv).rgb * 2.0 - 1.0);
            minNormalAgreement = min(minNormalAgreement, dot(centerNormal, sampleNormal));
        }
    }

    const float4 historySample = pass.quality > 0
        ? sampleCatmullRom(historyTexture, historyUv)
        : historyTexture.Sample(historyUv);
    moment1 /= 9.0;
    moment2 /= 9.0;
    const float3 sigma = sqrt(max(moment2 - moment1 * moment1, 0.0));
    // A one-sigma clip collapses legitimate alternating sub-pixel coverage at
    // silhouettes back toward the current jagged sample. A modest production-
    // style variance expansion retains those samples while the depth, normal,
    // velocity and HDR bounds below still reject stale history.
    const float varianceGamma = 1.25;
    minColor = max(minColor, moment1 - sigma * varianceGamma);
    maxColor = min(maxColor, moment1 + sigma * varianceGamma);
    const float3 clippedHistoryMapped = clipHistoryToNeighborhood(
        tonemap(historySample.rgb), minColor, maxColor);
    const float3 history = inverseTonemap(clippedHistoryMapped);

    const float3 currentNormal = centerNormal;
    const float3 historyNormal = normalize(normalTexture.Sample(historyUv).rgb * 2.0 - 1.0);
    const float historyDepth = depthTexture.Sample(historyUv).r;
    const float depthValidity = 1.0 - smoothstep(pass.parameters[1].x,
                                                 pass.parameters[1].x * 2.0,
                                                 abs(currentDepth - historyDepth));
    const float normalAgreement = dot(currentNormal, historyNormal);
    const float normalValidity = smoothstep(pass.parameters[1].y - 0.12,
                                            pass.parameters[1].y, normalAgreement);
    const float motionPixels = length(motion / max(pass.inverseResolution, float2(0.000001)));
    const float disocclusionWeight = smoothstep(0.35, 2.0, motionPixels);
    float validity = lerp(1.0, depthValidity * normalValidity, disocclusionWeight);
    validity *= saturate(1.0 - length(motion) * pass.parameters[1].z);
    const float lightingDelta = abs(luma(history) - luma(current.rgb)) / max(luma(current.rgb), 0.05);
    const float lightingValidity = 1.0 - smoothstep(0.08, 0.30, lightingDelta);
    validity *= lerp(1.0, lightingValidity, disocclusionWeight);

    const float3 lowpass = spatial / max(spatialWeight, 0.0001);
    const float localContrast = (maxLuma - minLuma) / max(maxLuma, 0.01);
    const float detailContrast = length(current.rgb - lowpass) /
                                 max(centerLuma + 0.05, 0.05);
    const float detailHistoryScale = lerp(1.0, 0.82,
        saturate(max(localContrast * 0.75, detailContrast * 0.65)));
    const float motionWeight = lerp(pass.parameters[0].y, pass.parameters[0].z,
                                    saturate(motionPixels * 0.35));
    const float interiorHistoryWeight = min(pass.parameters[0].x, motionWeight) *
                                        detailHistoryScale;
    const float depthEdge = smoothstep(pass.parameters[1].x,
                                       pass.parameters[1].x * 4.0,
                                       maxDepth - minDepth);
    const float normalEdge = 1.0 - smoothstep(pass.parameters[1].y - 0.20,
                                              pass.parameters[1].y,
                                              minNormalAgreement);
    // Require a strong luminance transition so ordinary texture detail is not
    // treated like a silhouette and given long-lived history.
    const float luminanceEdge = smoothstep(0.22, 0.55, localContrast);
    const float edgeConfidence = saturate(max(depthEdge, max(normalEdge, luminanceEdge)));
    const float stableEdgeWeight = lerp(0.96, pass.parameters[0].z,
                                        saturate(motionPixels * 0.35));
    const float requestedHistoryWeight = lerp(interiorHistoryWeight,
                                               stableEdgeWeight,
                                               edgeConfidence);
    const float historyWeight = clamp(requestedHistoryWeight * validity, 0.0, 0.96);
    float3 resolved = lerp(current.rgb, history, historyWeight);
    if (pass.parameters[0].w > 0.0)
    {
        const float3 sharpenedCurrent = current.rgb + (current.rgb - lowpass) * pass.parameters[0].w;
        const float sharpenDetailMask = 1.0 - smoothstep(0.08, 0.35, localContrast);
        resolved = lerp(resolved, sharpenedCurrent,
                        saturate(pass.parameters[0].w * sharpenDetailMask *
                                 (0.25 + 0.5 * (1.0 - historyWeight))));
    }
    const float3 hdrExtent = max(maxHdrColor - minHdrColor, 0.001);
    resolved = clamp(resolved, minHdrColor - hdrExtent * 0.35,
                     maxHdrColor + hdrExtent * 0.35);
    return float4(max(resolved, 0.0), currentDepth);
}
