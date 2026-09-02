struct VoxelParameters
{
    float3 volumeOrigin;
    float volumeSize;
    uint resolution;
    uint hasDirectionalLight;
    uint2 padding;
    float4 lightDirectionIntensity;
    float4 lightColor;
};
struct ObjectParameters { column_major float4x4 model; };
struct MaterialParameters
{
    float4 baseColor;
    float2 uvScale;
    float metallic;
    float alphaCutoff;
    float3 emission;
    uint alphaMode;
    uint hasAlbedoTexture;
    uint3 materialPadding;
};
[[vk::binding(0, 0)]] cbuffer VoxelConstants : register(b0, space0) { VoxelParameters voxel; };
[[vk::binding(1, 0)]] cbuffer ObjectConstants : register(b1, space0) { ObjectParameters object; };
[[vk::binding(2, 0)]] cbuffer MaterialConstants : register(b2, space0) { MaterialParameters material; };
[[vk::binding(3, 0)]] Sampler2D albedoTexture : register(t3, space0);
[[vk::binding(4, 0)]] RWTexture3D<uint> accumulationR : register(u4, space0);
[[vk::binding(5, 0)]] RWTexture3D<uint> accumulationG : register(u5, space0);
[[vk::binding(6, 0)]] RWTexture3D<uint> accumulationB : register(u6, space0);
[[vk::binding(7, 0)]] RWTexture3D<uint> accumulationWeight : register(u7, space0);

struct VertexInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VertexOutput { float3 worldPosition : POSITION0; float3 worldNormal : NORMAL0; float2 uv : TEXCOORD0; };
struct GeometryOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION0;
    float3 worldNormal : NORMAL0;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.worldPosition = mul(object.model, float4(input.position, 1.0)).xyz;
    output.worldNormal = normalize(mul((float3x3)object.model, input.normal));
    output.uv = input.uv * material.uvScale;
    return output;
}

float2 projectAxis(float3 position, uint axis)
{
    return axis == 0 ? position.zy : axis == 1 ? position.xz : position.xy;
}

[shader("geometry")]
[maxvertexcount(3)]
void geometryMain(triangle VertexOutput vertices[3], inout TriangleStream<GeometryOutput> stream)
{
    const float3 faceNormal = abs(cross(vertices[1].worldPosition - vertices[0].worldPosition,
                                        vertices[2].worldPosition - vertices[0].worldPosition));
    const uint axis = faceNormal.y > faceNormal.x
                          ? (faceNormal.z > faceNormal.y ? 2 : 1)
                          : (faceNormal.z > faceNormal.x ? 2 : 0);
    for (uint index = 0; index < 3; ++index)
    {
        GeometryOutput output;
        const float3 normalized = (vertices[index].worldPosition - voxel.volumeOrigin) /
                                  voxel.volumeSize * 2.0 - 1.0;
        output.position = float4(projectAxis(normalized, axis), 0.0, 1.0);
        output.worldPosition = vertices[index].worldPosition;
        output.worldNormal = vertices[index].worldNormal;
        output.uv = vertices[index].uv;
        stream.Append(output);
    }
    stream.RestartStrip();
}

[shader("fragment")]
void fragmentMain(GeometryOutput input)
{
    const float3 textureCoordinate = (input.worldPosition - voxel.volumeOrigin) / voxel.volumeSize;
    if (any(textureCoordinate < 0.0) || any(textureCoordinate >= 1.0)) discard;
    float4 albedo = material.baseColor;
    if (material.hasAlbedoTexture != 0) albedo *= albedoTexture.Sample(input.uv);
    if (material.alphaMode == 1 && albedo.a < material.alphaCutoff) discard;
    const float coverage = material.alphaMode == 2 ? saturate(albedo.a) : 1.0;
    const float3 normal = normalize(input.worldNormal);
    float3 direct = 0.0;
    if (voxel.hasDirectionalLight != 0)
        direct = voxel.lightColor.rgb * voxel.lightDirectionIntensity.w *
                 max(dot(normal, normalize(-voxel.lightDirectionIntensity.xyz)), 0.0);
    float3 radiance = albedo.rgb * (1.0 - saturate(material.metallic)) * direct *
                      (coverage / 3.14159265) + max(material.emission, 0.0) * coverage;
    radiance = clamp(radiance, 0.0, 16.0);
    const uint3 encoded = uint3(round(radiance * (4095.0 / 16.0)));
    const uint encodedOpacity = uint(round(coverage * 4095.0));
    const int3 coordinate = clamp(int3(textureCoordinate * float(voxel.resolution)),
                                  int3(0), int3(voxel.resolution - 1));
    InterlockedAdd(accumulationR[coordinate], encoded.r);
    InterlockedAdd(accumulationG[coordinate], encoded.g);
    InterlockedAdd(accumulationB[coordinate], encoded.b);
    InterlockedAdd(accumulationWeight[coordinate], encodedOpacity);
}
