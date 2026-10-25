#include "Fullscreen.slang"

// Editor selection outline. Selected geometry is rasterized depth-only into
// selectionDepth (reverse-Z, cleared to zero), then this pass blends a
// constant-width outline around its silhouette onto the display output.
[[vk::binding(0, 0)]] cbuffer SelectionOutlineParameters : register(b0, space0)
{
    float4 outlineColor;
    // Radius in output pixels, alpha scale where the selection is hidden
    // behind other geometry, padding.
    float4 outlineSettings;
    // Output target width and height, padding.
    float4 outputSize;
};

[[vk::binding(1, 0)]] Sampler2D selectionDepth : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D sceneDepth : register(t2, space0);

static const int kMaxRadius = 6;

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    uint maskWidth, maskHeight;
    selectionDepth.GetDimensions(maskWidth, maskHeight);
    const int2 maskSize = int2(maskWidth, maskHeight);
    // Depth targets share the render resolution, which may differ from the
    // output resolution when the view is scaled or upscaled.
    const float2 outputToMask = float2(maskSize) / max(outputSize.xy, 1.0);
    const float2 outputPixel = input.uv * outputSize.xy;
    const int2 centerTexel = clamp(int2(outputPixel * outputToMask), int2(0), maskSize - 1);
    // The outline surrounds the selection; its interior stays untouched.
    if (selectionDepth.Load(int3(centerTexel, 0)).r > 0.0)
        return float4(0.0);

    const float radius = clamp(outlineSettings.x, 0.5, float(kMaxRadius));
    const int reach = int(ceil(radius));
    float nearestVisible = 1.0e9;
    float nearestHidden = 1.0e9;
    for (int y = -kMaxRadius; y <= kMaxRadius; ++y)
    {
        if (abs(y) > reach)
            continue;
        for (int x = -kMaxRadius; x <= kMaxRadius; ++x)
        {
            if (abs(x) > reach)
                continue;
            const float distance = length(float2(x, y));
            if (distance > radius + 0.5)
                continue;
            const int2 texel = clamp(int2((outputPixel + float2(x, y)) * outputToMask), int2(0), maskSize - 1);
            const float selected = selectionDepth.Load(int3(texel, 0)).r;
            if (selected <= 0.0)
                continue;
            // Reverse-Z: a larger scene depth is nearer. Allow for precision
            // and temporal jitter, which only the scene depth carries.
            const float scene = sceneDepth.Load(int3(texel, 0)).r;
            if (scene > selected * 1.002 + 1.0e-6)
                nearestHidden = min(nearestHidden, distance);
            else
                nearestVisible = min(nearestVisible, distance);
        }
    }
    // Integer taps give a one-pixel ramp at the outer edge, which keeps the
    // line smooth without a separate filtering pass.
    const float visibleCoverage = saturate(radius + 0.5 - nearestVisible);
    const float hiddenCoverage = saturate(radius + 0.5 - nearestHidden) * outlineSettings.y;
    const float coverage = max(visibleCoverage, hiddenCoverage);
    if (coverage <= 0.0)
        return float4(0.0);
    return float4(outlineColor.rgb, outlineColor.a * coverage);
}
