#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

// Camera motion for pixels no geometry covered (sky, distant background).
// The geometry pass clears motion to zero there, which tells temporal
// reconstruction the background is glued to the screen while the camera
// turns, smearing clouds and anything drawn behind the far plane. Treat the
// background as infinitely distant: its motion is the rotation of its view
// direction between the previous and current unjittered view-projections.
//
// pass.view / pass.projection: current unjittered camera.
// pass.worldToLocal: previous unjittered view-projection.

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

[shader("fragment")]
float2 fragmentMain(FullscreenVertexOutput input) : SV_Target0
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    // Reverse-Z clears to zero; covered pixels keep the motion geometry wrote.
    if (depthTexture.SampleLevel(uv, 0.0).r > 0.000001) discard;
    // A direction, not an unprojected far-plane point: points at infinity
    // ignore camera translation, and some OpenGL drivers lose precision when
    // unprojecting at the reverse-Z far plane (see reconstructWorldRay).
    const float4 direction = float4(reconstructWorldRay(uv, pass.view, pass.projection, pass.flipY), 0.0);
    const float4 current = mul(pass.projection, mul(pass.view, direction));
    const float4 previous = mul(pass.worldToLocal, direction);
    // Behind the previous camera there is no history to reproject.
    if (current.w <= 0.000001 || previous.w <= 0.000001) return float2(0.0);
    // Same convention as BasicLit: current-minus-previous NDC, halved.
    return (current.xy / current.w - previous.xy / previous.w) * 0.5;
}
