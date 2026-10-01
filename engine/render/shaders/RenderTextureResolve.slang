#include "Fullscreen.slang"

// Copies a camera's display output into a render texture's sRGB image.
[[vk::binding(0, 0)]] cbuffer RenderTextureResolveParameters : register(b0, space0)
{
    // x: flip Y, zw: reciprocal target size.
    float4 options;
};

[[vk::binding(1, 0)]] Sampler2D source : register(t1, space0);

float3 srgbToLinear(float3 value)
{
    const float3 low = value / 12.92;
    const float3 high = pow((value + 0.055) / 1.055, 2.4);
    return select(value <= 0.04045, low, high);
}

[shader("vertex")] FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

[shader("fragment")] float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    float2 uv = input.position.xy * options.zw;
    uv.y = options.x > 0.5 ? 1.0 - uv.y : uv.y;
    // Display output is already encoded for the screen. Decoding here lets
    // the sRGB target store those exact values, so materials sample the
    // render texture like an imported screenshot.
    return float4(srgbToLinear(saturate(source.SampleLevel(uv, 0).rgb)), 1.0);
}
