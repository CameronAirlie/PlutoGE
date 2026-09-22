#include "Fullscreen.slang"
[[vk::binding(0, 0)]] cbuffer PortraitParameters : register(b0, space0)
{
    float4 options;
};
[[vk::binding(1, 0)]] Sampler2D source : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depth : register(t2, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}
[shader("fragment")] float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    uint width, height;
    source.GetDimensions(width, height);
    float2 uv = input.position.xy / float2(width, height);
    // Reversed depth clears to zero. Remove the scene background, keeping premultiplied RGBA.
    float2 depthUv = float2(uv.x, options.x > 0.5 ? 1.0 - uv.y : uv.y);
    float coverage = depth.SampleLevel(depthUv, 0).r > 0.000001 ? 1.0 : 0.0;
    return float4(source.SampleLevel(uv, 0).rgb * coverage, coverage);
}
