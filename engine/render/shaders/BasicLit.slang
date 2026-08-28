struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};

[[vk::binding(0, 0)]] cbuffer CameraParameters : register(b0, space0)
{
    column_major float4x4 viewProjection;
    float4 cameraPositionAmbient;
    float4 directionalDirectionIntensity;
    float4 directionalColor;
};

[[vk::binding(0, 2)]] cbuffer ObjectParameters : register(b0, space2)
{
    column_major float4x4 model;
};

[[vk::binding(0, 1)]] cbuffer MaterialParameters : register(b0, space1)
{
    float4 baseColor;
    float2 uvScale;
    float metallic;
    float roughness;
    float3 emission;
    float alphaCutoff;
    uint alphaMode;
    float3 materialPadding;
};

[[vk::binding(1, 1)]] Sampler2D baseColorTexture : register(t1, space1);

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(model, float4(input.position, 1.0));
    output.position = mul(viewProjection, worldPosition);
    output.worldNormal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 sampledColor = baseColorTexture.Sample(input.uv * uvScale) * baseColor;
    if (alphaMode == 1 && sampledColor.a < alphaCutoff)
        discard;

    const float3 normal = normalize(input.worldNormal);
    const float3 lightDirection = normalize(-directionalDirectionIntensity.xyz);
    const float3 viewDirection = normalize(cameraPositionAmbient.xyz - input.worldPosition);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float ndotl = saturate(dot(normal, lightDirection));
    const float ndoth = saturate(dot(normal, halfDirection));
    const float perceptualRoughness = clamp(roughness, 0.04, 1.0);
    const float specularPower = lerp(256.0, 4.0, perceptualRoughness);
    const float3 f0 = lerp(float3(0.04), sampledColor.rgb, saturate(metallic));
    const float3 diffuse = sampledColor.rgb * (1.0 - saturate(metallic)) * ndotl;
    const float3 specular = f0 * pow(ndoth, specularPower) * ndotl;
    const float3 direct = (diffuse + specular) * directionalColor.rgb * directionalDirectionIntensity.w;
    const float3 ambient = sampledColor.rgb * cameraPositionAmbient.w;
    return float4(ambient + direct + emission, sampledColor.a);
}
