#include "Fullscreen.slang"

// Composites one overlay camera over the camera stack's colour target.
[[vk::binding(0, 0)]] cbuffer CameraStackParameters : register(b0, space0)
{
    // x: flip depth Y, zw: reciprocal target size.
    float4 options;
};

[[vk::binding(1, 0)]] Sampler2D overlayColor : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D overlayDepth : register(t2, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

[shader("fragment")] float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = input.position.xy * options.zw;
    // Display output and geometry depth differ in orientation on Vulkan.
    const float2 depthUv = float2(uv.x, options.x > 0.5 ? 1.0 - uv.y : uv.y);
    // Reversed depth clears to zero, so any written depth is overlay geometry.
    const float coverage = overlayDepth.SampleLevel(depthUv, 0).r > 0.000001 ? 1.0 : 0.0;
    // Premultiplied output; the pipeline blends with (one, one - source alpha).
    return float4(overlayColor.SampleLevel(uv, 0).rgb * coverage, coverage);
}
