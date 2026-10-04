#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0)
{
    float4x4 inverseViewProjection, inverseModel;
    float4 color, projectorNormal, material, viewport;
};
[[vk::binding(1, 0)]] Sampler2D sceneColor : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D sceneAlbedo : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D sceneDepth : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D sceneNormal : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D decalTexture : register(t5, space0);
struct DecalOutput { float4 color : SV_Target0; float4 albedo : SV_Target1; };
[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }
[shader("fragment")]
DecalOutput snapshotMain(FullscreenVertexOutput input)
{
    uint width, height; sceneColor.GetDimensions(width, height);
    float2 uv = input.position.xy / float2(width, height);
    DecalOutput result;
    result.color = sceneColor.SampleLevel(uv, 0);
    result.albedo = sceneAlbedo.SampleLevel(uv, 0);
    return result;
}
[shader("fragment")]
DecalOutput fragmentMain(FullscreenVertexOutput input)
{
    float2 uv = input.position.xy * viewport.xy;
    float depth = sceneDepth.SampleLevel(uv, 0).r;
    if (depth <= 0.000001) discard;
    uint conventions = uint(viewport.z);
    float3 world = reconstructWorldPosition(uv, depth, inverseViewProjection, conventions & 1, (conventions >> 1) & 1);
    float3 local = mul(inverseModel, float4(world, 1)).xyz;
    if (any(abs(local) > 0.5)) discard;
    float3 normal = decodeNormal(sceneNormal.SampleLevel(uv, 0).xyz);
    if (dot(normal, projectorNormal.xyz) < projectorNormal.w) discard;
    float4 sample = viewport.w > 0.5 ? decalTexture.Sample((local.xy + 0.5) * material.xy) : float4(1);
    if (material.z == 1 && sample.a < material.w) discard;
    // Lifetime fading is independent of the material's alpha-test coverage.
    float alpha = material.z == 2 ? sample.a * color.a : color.a;
    if (alpha <= 0.001) discard;
    float4 albedo = sceneAlbedo.SampleLevel(uv, 0);
    float3 illumination = sceneColor.SampleLevel(uv, 0).rgb / max(albedo.rgb, float3(0.04));
    float3 decal = sample.rgb * color.rgb;
    DecalOutput result;
    result.color = float4(decal * illumination * alpha, alpha);
    // The blend state preserves receiver coverage in both attachment alphas.
    result.albedo = float4(decal * alpha, alpha);
    return result;
}
