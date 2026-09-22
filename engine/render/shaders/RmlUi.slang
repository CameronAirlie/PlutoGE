struct VertexInput
{
    float2 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
cbuffer RmlUiParameters : register(b0, space0)
{
    column_major float4x4 transform;
    float2 translation;
    float clipYSign;
    float maskMode;
    float2 inverseSize;
    float flipTextureY;
    float padding;
};

[[vk::binding(0, 1)]] Sampler2D sourceTexture : register(t0, space1);

[[vk::binding(1, 1)]] Sampler2D clipTexture : register(t1, space1);

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(transform, float4(input.position + translation, 0.0, 1.0));
    // Vulkan's negative-height viewport and RmlUi's top-left orthographic
    // projection otherwise invert Y twice. OpenGL uses +1; Vulkan uses -1.
    output.position.y *= clipYSign;
    output.color = input.color;
    output.uv = float2(input.uv.x, flipTextureY > 0.5 ? 1.0 - input.uv.y : input.uv.y);
    return output;
}

 [shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    if (maskMode == 1.0) return float4(1.0); // Set coverage.
    if (maskMode == 3.0) return float4(0.0); // Inverse: erase from a white mask.
    float coverage = (maskMode == 2.0 || maskMode == 4.0)
        ? clipTexture.Sample(input.position.xy * inverseSize).r : 1.0;
    if (maskMode == 2.0) return float4(coverage); // Intersection.
    return input.color * sourceTexture.Sample(input.uv) * coverage;
}
