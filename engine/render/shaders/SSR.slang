#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D materialTexture : register(t4, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return scene;
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const float4 material = materialTexture.Sample(uv);
    const float metallic = saturate(material.r * max(pass.parameters[1].z, 0.0));
    const float roughness = saturate(material.g);
    if (metallic <= 0.001) return scene;
    const float3 viewDirection = normalize(float3(uv * 2.0 - 1.0, 1.0));
    const float3 reflected = reflect(viewDirection, normal);
    float2 rayUv = uv + reflected.xy * pass.parameters[0].w;
    float3 reflection = 0.0;
    float hit = 0.0;
    const uint steps = clamp(pass.quality, 8u, 64u);
    for (uint i = 0; i < steps && hit < 0.5; ++i)
    {
        rayUv += reflected.xy * pass.parameters[0].y * pass.inverseResolution;
        if (any(rayUv < 0.0) || any(rayUv > 1.0)) break;
        const float rayDepth = depthTexture.Sample(rayUv).r;
        if (abs(rayDepth - depth) < pass.parameters[0].z * 0.01 && rayDepth > 0.000001)
        {
            reflection = sceneTexture.Sample(rayUv).rgb;
            hit = 1.0;
        }
    }
    const float fresnel = pow(1.0 - saturate(dot(-viewDirection, normal)),
                              max(pass.parameters[1].y, 0.1));
    const float visibility = hit * edgeFade(rayUv, pass.parameters[1].x) *
                             metallic * (1.0 - roughness) * pass.parameters[0].x;
    return float4(lerp(scene.rgb, reflection * (0.5 + 0.5 * fresnel), saturate(visibility)), scene.a);
}
