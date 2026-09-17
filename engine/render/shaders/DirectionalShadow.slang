float2 shaderGraphScreenUV(float3 p){return float2(0);}
float4 shaderGraphTexture(int slot, float2 uv);
#include "ShaderGraphEvaluation.slang"
[[vk::binding(0, 1)]]
cbuffer ShadowMaterial : register(b0, space1)
{
    float4 shadowAlpha;
    float4 graphColor;
    float4 graphFactors; // metallic, roughness, alpha mode
    float4 graphEmission;
    float4 graphCameraTime;
    ShaderGraphData graph;
};
[[vk::binding(1, 1)]] Sampler2D shadowBaseColor : register(t1, space1);
[[vk::binding(2, 1)]] Sampler2D graphNormalTexture : register(t2, space1);
[[vk::binding(3, 1)]] Sampler2D graphMetallicTexture : register(t3, space1);
[[vk::binding(4, 1)]] Sampler2D graphRoughnessTexture : register(t4, space1);
[[vk::binding(14,1)]] Sampler2D graphExtra0 : register(t14,space1);
[[vk::binding(15,1)]] Sampler2D graphExtra1 : register(t15,space1);
[[vk::binding(16,1)]] Sampler2D graphExtra2 : register(t16,space1);
[[vk::binding(17,1)]] Sampler2D graphExtra3 : register(t17,space1);
float4 shaderGraphTexture(int slot, float2 uv)
{
    if(slot==4)return graphExtra0.SampleLevel(uv,0.0);
    if(slot==5)return graphExtra1.SampleLevel(uv,0.0);
    if(slot==6)return graphExtra2.SampleLevel(uv,0.0);
    if(slot==7)return graphExtra3.SampleLevel(uv,0.0);
    if(slot==0) return shadowBaseColor.SampleLevel(uv,0.0);
    if(slot==1) return graphNormalTexture.SampleLevel(uv,0.0);
    if(slot==2) return graphMetallicTexture.SampleLevel(uv,0.0);
    return graphRoughnessTexture.SampleLevel(uv,0.0);
}
struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
cbuffer ShadowCamera : register(b0, space0)
{
    column_major float4x4 lightViewProjection;
};
[[vk::binding(0, 2)]]
cbuffer ShadowObject : register(b0, space2)
{
    column_major float4x4 model;
};

[[vk::binding(0, 3)]]
cbuffer InstancedShadowObjects : register(b0, space3)
{
    column_major float4x4 instanceModels[64];
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float3 world = mul(model, float4(input.position,1.0)).xyz;
    float3 normal = normalize(mul((float3x3)model,input.normal));
    world += shaderGraphVertexOffset(graph,world,normal,normalize(graphCameraTime.xyz-world),
        graphCameraTime.w,input.uv*shadowAlpha.xy,graphColor,graphFactors.x,graphFactors.y,graphEmission.xyz);
    output.worldPosition = world;
    output.worldNormal = normal;
    output.position = mul(lightViewProjection,float4(world,1.0));
    output.uv = input.uv;
    return output;
}

[shader("vertex")]
// RHI instanced draws always use firstInstance = 0; use the raw backend index.
VertexOutput instancedVertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID)
{
    VertexOutput output;
    float3 world = mul(instanceModels[instanceId], float4(input.position,1.0)).xyz;
    float3 normal = normalize(mul((float3x3)instanceModels[instanceId],input.normal));
    world += shaderGraphVertexOffset(graph,world,normal,normalize(graphCameraTime.xyz-world),
        graphCameraTime.w,input.uv*shadowAlpha.xy,graphColor,graphFactors.x,graphFactors.y,graphEmission.xyz);
    output.worldPosition = world;
    output.worldNormal = normal;
    output.position = mul(lightViewProjection,float4(world,1.0));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Target
{
    return input.position.z;
}

[shader("fragment")]
float maskedFragmentMain(VertexOutput input) : SV_Target
{
    const float4 sampledBaseColor=shadowBaseColor.Sample(input.uv*shadowAlpha.xy);
    float4 color=sampledBaseColor*graphColor;
    float3 normal=normalize(input.worldNormal), emission=graphEmission.xyz*sampledBaseColor.rgb;
    float metallic=graphFactors.x, roughness=graphFactors.y;
    evaluateShaderGraph(graph,input.worldPosition,normal,normalize(graphCameraTime.xyz-input.worldPosition),
        graphCameraTime.w,input.uv*shadowAlpha.xy,color,normal,metallic,roughness,emission);
    if(graphFactors.z==1.0 && color.a<shadowAlpha.z) discard;
    return input.position.z;
}
