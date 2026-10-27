// Standard materials for GPU-driven draws (BasicLitBindless.slang). A draw's instance record names a material
// record, whose texture indices select entries of the device's bindless table. The values BasicLit reads from
// MaterialParameters become per-invocation statics that loadBindlessMaterial fills; graph and glass inputs keep
// neutral values, since only opaque standard materials draw this way. Layouts match GpuSceneTables.h.
// Requires ShaderGraphCommon.slang (ShaderGraphData).

// Every member is 16 bytes, so storage and uniform layouts agree.
struct BindlessMaterial
{
    float4 baseColor;
    float4 uvScaleMetallicRoughness;
    float4 emissionAlphaCutoff;
    uint4 flags;       // alpha mode, texture/normal flags (BindlessMaterialFlag*), metallic channel, roughness channel
    float4 subsurfaceColorStrength;
    float4 subsurfaceRadiusPadding;
    float4 glassParameters;
    float4 attenuationColorDistance;
    uint4 emissionParameters;
    float4 emissionChannels[3];
    uint4 textures;    // base colour, normal, metallic, roughness
    uint4 emissionTexture;
};

struct BindlessInstance
{
    column_major float4x4 model;
    column_major float4x4 previousModel;
    float4 debugParameters;
    uint4 material;
};

static const uint BindlessMaterialHasNormal = 1;
static const uint BindlessMaterialHasMetallic = 2;
static const uint BindlessMaterialHasRoughness = 4;
static const uint BindlessMaterialFlipNormalY = 8;
static const uint BindlessMaterialTwoSided = 16;

[[vk::binding(0, 4)]] Sampler2D bindlessTextures[] : register(t0, space4);
[[vk::binding(1, 2)]] StructuredBuffer<BindlessInstance> bindlessInstances : register(t1, space2);
[[vk::binding(2, 2)]] StructuredBuffer<BindlessMaterial> bindlessMaterials : register(t2, space2);

// Stands in for a material Sampler2D. The index varies within a multi-draw, so it is non-uniform.
struct BindlessTexture
{
    uint index;
    float4 SampleGrad(float2 uv, float2 dx, float2 dy)
    {
        return bindlessTextures[NonUniformResourceIndex(index)].SampleGrad(uv, dx, dy);
    }
    float4 SampleLevel(float2 uv, float level)
    {
        return bindlessTextures[NonUniformResourceIndex(index)].SampleLevel(uv, level);
    }
};

static float4 baseColor;
static float2 uvScale;
static float metallic;
static float roughness;
static float3 emission;
static float alphaCutoff;
static uint alphaMode;
static uint hasNormalTexture;
static uint hasMetallicTexture;
static uint hasRoughnessTexture;
static uint metallicChannel;
static uint roughnessChannel;
static uint flipNormalY;
static uint materialTwoSided;
static float4 subsurfaceColorStrength;
static float4 subsurfaceRadiusPadding;
static float4 glassParameters;
static float4 attenuationColorDistance;
static float4 glassViewport;
static float4 glassFog[4];
static ShaderGraphData shaderGraph;
static float4 shaderGraphFrame;
static uint4 emissionParameters;
static float4 emissionChannels[3];
static BindlessTexture baseColorTexture, normalTexture, metallicTexture, roughnessTexture, emissionTexture;

void loadBindlessMaterial(uint index)
{
    const BindlessMaterial material = bindlessMaterials[NonUniformResourceIndex(index)];
    baseColor = material.baseColor;
    uvScale = material.uvScaleMetallicRoughness.xy;
    metallic = material.uvScaleMetallicRoughness.z;
    roughness = material.uvScaleMetallicRoughness.w;
    emission = material.emissionAlphaCutoff.xyz;
    alphaCutoff = material.emissionAlphaCutoff.w;
    alphaMode = material.flags.x;
    hasNormalTexture = (material.flags.y & BindlessMaterialHasNormal) != 0 ? 1u : 0u;
    hasMetallicTexture = (material.flags.y & BindlessMaterialHasMetallic) != 0 ? 1u : 0u;
    hasRoughnessTexture = (material.flags.y & BindlessMaterialHasRoughness) != 0 ? 1u : 0u;
    flipNormalY = (material.flags.y & BindlessMaterialFlipNormalY) != 0 ? 1u : 0u;
    materialTwoSided = (material.flags.y & BindlessMaterialTwoSided) != 0 ? 1u : 0u;
    metallicChannel = material.flags.z;
    roughnessChannel = material.flags.w;
    subsurfaceColorStrength = material.subsurfaceColorStrength;
    subsurfaceRadiusPadding = material.subsurfaceRadiusPadding;
    glassParameters = material.glassParameters;
    attenuationColorDistance = material.attenuationColorDistance;
    glassViewport = float4(0.0);
    for (int index = 0; index < 4; ++index)
        glassFog[index] = float4(0.0);
    // Standard shading reads only the graph header (no graph); the rest is never evaluated.
    shaderGraph.header = int4(0);
    shaderGraph.outputs0 = int4(0);
    shaderGraph.outputs1 = int4(0);
    shaderGraphFrame = float4(0.0);
    emissionParameters = material.emissionParameters;
    emissionChannels = material.emissionChannels;
    baseColorTexture.index = material.textures.x;
    normalTexture.index = material.textures.y;
    metallicTexture.index = material.textures.z;
    roughnessTexture.index = material.textures.w;
    emissionTexture.index = material.emissionTexture.x;
}
