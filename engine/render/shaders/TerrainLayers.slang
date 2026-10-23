// Terrain material layers for the BasicLitTerrain variant (PLUTO_TERRAIN_LAYERS). C++ side:
// PlutoGE/render/TerrainLayers.h (data and array packing) and BasicTerrainLayers (bindings, parameters).
//
// Layer i's weight is channel i % 4 of weight map i / 4, over the terrain's 0..1 UVs. Each layer's maps sit in
// array layer i of two arrays, so a layer costs two texture fetches: albedo (sRGB colour, height in alpha) and
// normal (OpenGL tangent normal, roughness in alpha). Layer maps tile in world space (x and z), so a layer's tile
// size is in world units.
//
// Cost: beyond the detail distance the terrain shades from its macro albedo instead (terrainDetailAmount; like
// a "base map distance"), pixels inside one layer take a single-layer path; elsewhere every layer's weight is read but only the
// kTerrainShadedLayers strongest are shaded (blends of more than four layers at one point are imperceptible), and
// the albedo fetched for height blending is reused for colour. Layers with anti-tiling fetch each map twice.
//
// OpenGL flattening (NormalizeOpenGLShader.cmake): textures 20..23 -> units 3..6, parameters 25 -> block 4.

static const uint kTerrainMaxLayers = 8;
static const uint kTerrainShadedLayers = 4;

[[vk::binding(20, 1)]] Sampler2D terrainWeights0 : register(t20, space1);
[[vk::binding(21, 1)]] Sampler2D terrainWeights1 : register(t21, space1);
[[vk::binding(22, 1)]] Sampler2DArray terrainAlbedo : register(t22, space1); // rgb colour, a height
[[vk::binding(23, 1)]] Sampler2DArray terrainNormal : register(t23, space1); // xyz normal, a roughness

[[vk::binding(25, 1)]]
cbuffer TerrainLayerParameters : register(b25, space1)
{
    float4 terrainTiling[kTerrainMaxLayers]; // x 1 / tile size, y normal strength, z roughness, w metallic
    float4 terrainTint[kTerrainMaxLayers];   // rgb tint, w anti-tiling (0 or 1)
    float4 terrainParameters;                // x layer count, y height blend, z detail distance (0 = everywhere), w fade
};

struct TerrainSurface
{
    float3 albedo;
    float3 normal; // world space
    float roughness;
    float metallic;
};

// Below this weight a layer is skipped: it cannot change an 8-bit result.
static const float kTerrainMinimumWeight = 1.0 / 255.0;

[ForceInline]
float terrainWeight(float4 weights0, float4 weights1, uint layer)
{
    const float4 weights = layer < 4 ? weights0 : weights1;
    const uint channel = layer & 3;
    return channel == 0 ? weights.x : channel == 1 ? weights.y : channel == 2 ? weights.z : weights.w;
}

// ---- anti-tiling ------------------------------------------------------------------------------------------------
// Stochastic tiling (after Quilez, "texture repetition", technique 3): a slowly varying noise over the layer's UVs
// picks between two offset copies of its maps, so repeats never line up into a visible grid. Offsets only (no
// rotation) keep tangent-space normals valid; every map of a layer uses the same variation.

struct TerrainTileVariation
{
    bool enabled;
    float2 offsetA, offsetB;
    float blend;
};

float terrainHash(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float terrainValueNoise(float2 p)
{
    const float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(terrainHash(cell), terrainHash(cell + float2(1, 0)), f.x),
                lerp(terrainHash(cell + float2(0, 1)), terrainHash(cell + float2(1, 1)), f.x), f.y);
}

TerrainTileVariation terrainTileVariation(float2 uv, bool enabled)
{
    TerrainTileVariation variation;
    variation.enabled = enabled;
    variation.offsetA = variation.offsetB = 0.0;
    variation.blend = 0.0;
    if (enabled)
    {
        // Patches a few repeats across, each with its own offset; neighbours cross-fade.
        const float k = terrainValueNoise(uv * 0.25) * 8.0;
        const float index = floor(k);
        variation.offsetA = sin(float2(3.0, 7.0) * index);
        variation.offsetB = sin(float2(3.0, 7.0) * (index + 1.0));
        variation.blend = smoothstep(0.2, 0.8, k - index);
    }
    return variation;
}

float4 sampleTerrainArray(Sampler2DArray map, float2 uv, uint layer, float2 dx, float2 dy, TerrainTileVariation variation)
{
    if (!variation.enabled)
        return map.SampleGrad(float3(uv, float(layer)), dx, dy);
    const float4 a = map.SampleGrad(float3(uv + variation.offsetA, float(layer)), dx, dy);
    const float4 b = map.SampleGrad(float3(uv + variation.offsetB, float(layer)), dx, dy);
    return lerp(a, b, variation.blend);
}

// ---- blending -------------------------------------------------------------------------------------------------

// How much of the layers shows at a distance from the camera: 1 within the detail distance, fading to 0 (the
// macro albedo alone) over the fade band beyond it.
float terrainDetailAmount(float cameraDistance)
{
    const float limit = terrainParameters.z;
    return limit <= 0.0 ? 1.0 : saturate((limit - cameraDistance) / max(terrainParameters.w, 0.001) + 1.0);
}

TerrainSurface sampleTerrainLayers(float2 terrainUv, float3 worldPosition, float3 worldDx, float3 worldDy,
                                   float3 geometricNormal, float4 worldTangent)
{
    const uint count = min(uint(terrainParameters.x + 0.5), kTerrainMaxLayers);
    const float heightBlend = terrainParameters.y;
    // Weights change slowly across the terrain; the base level avoids blurring painted edges at a distance.
    const float4 weights0 = terrainWeights0.SampleLevel(terrainUv, 0.0);
    const float4 weights1 = terrainWeights1.SampleLevel(terrainUv, 0.0);

    // Fast path: most of a terrain lies inside one layer (open dust, rock faces), where no blending is needed.
    uint only = kTerrainMaxLayers;
    [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
        if (layer < count && terrainWeight(weights0, weights1, layer) >= 1.0 - kTerrainMinimumWeight)
            only = layer;
    if (only < kTerrainMaxLayers || count <= 1)
    {
        const uint layer = only < kTerrainMaxLayers ? only : 0;
        const float4 tiling = terrainTiling[layer];
        const float2 uv = worldPosition.xz * tiling.x;
        const float2 dx = worldDx.xz * tiling.x;
        const float2 dy = worldDy.xz * tiling.x;
        const TerrainTileVariation variation = terrainTileVariation(uv, terrainTint[layer].w > 0.5);
        const float4 normalRoughness = sampleTerrainArray(terrainNormal, uv, layer, dx, dy, variation);
        float3 tangentNormal = normalRoughness.xyz * 2.0 - 1.0;
        tangentNormal.xy *= tiling.y;
        TerrainSurface surface;
        surface.albedo = sampleTerrainArray(terrainAlbedo, uv, layer, dx, dy, variation).rgb * terrainTint[layer].rgb;
        const float3 tangent = normalize(worldTangent.xyz);
        const float3 bitangent = normalize(cross(geometricNormal, tangent)) * worldTangent.w;
        surface.normal = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + geometricNormal * max(tangentNormal.z, 0.001));
        surface.roughness = clamp(normalRoughness.a * tiling.z, 0.04, 1.0);
        surface.metallic = saturate(tiling.w);
        return surface;
    }

    // Blend factors: the painted weights, or with height blending the layers whose height plus weight comes
    // within `heightBlend` of the highest (the higher surface shows through the transition).
    float blend[kTerrainMaxLayers];
    float4 albedoSample[kTerrainMaxLayers];
    uint sampledAlbedo = 0;
    float top = -1.0e9;
    [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
    {
        const float weight = layer < count ? terrainWeight(weights0, weights1, layer) : 0.0;
        blend[layer] = weight;
        albedoSample[layer] = 0.0;
        if (heightBlend > 0.0 && weight > kTerrainMinimumWeight)
        {
            const float scale = terrainTiling[layer].x;
            const float2 uv = worldPosition.xz * scale;
            albedoSample[layer] = sampleTerrainArray(terrainAlbedo, uv, layer, worldDx.xz * scale, worldDy.xz * scale,
                                                     terrainTileVariation(uv, terrainTint[layer].w > 0.5));
            sampledAlbedo |= 1u << layer;
            blend[layer] = weight + albedoSample[layer].a;
            top = max(top, blend[layer]);
        }
    }
    if (heightBlend > 0.0)
        [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
            blend[layer] = blend[layer] > kTerrainMinimumWeight ? max(blend[layer] - (top - heightBlend), 0.0) : 0.0;

    // Shade only the strongest layers (usually all of them: few points have more).
    uint shaded = 0;
    uint present = 0;
    [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
        if (blend[layer] > kTerrainMinimumWeight)
        {
            shaded |= 1u << layer;
            ++present;
        }
    if (present > kTerrainShadedLayers)
        shaded = 0;
    [unroll] for (uint pick = 0; present > kTerrainShadedLayers && pick < kTerrainShadedLayers; ++pick)
    {
        uint best = kTerrainMaxLayers;
        float bestBlend = kTerrainMinimumWeight;
        [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
            if ((shaded & (1u << layer)) == 0 && blend[layer] > bestBlend)
            {
                best = layer;
                bestBlend = blend[layer];
            }
        if (best < kTerrainMaxLayers)
            shaded |= 1u << best;
    }
    float total = 0.0;
    [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
        total += (shaded & (1u << layer)) != 0 ? blend[layer] : 0.0;
    // Unpainted texels show the first layer.
    if (shaded == 0)
    {
        shaded = 1;
        blend[0] = 1.0;
        total = 1.0;
    }

    float3 albedo = 0.0;
    float3 tangentNormal = 0.0;
    float roughness = 0.0;
    float metallic = 0.0;
    [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
    {
        if ((shaded & (1u << layer)) == 0)
            continue;
        const float amount = blend[layer] / total;
        const float4 tiling = terrainTiling[layer];
        const float2 uv = worldPosition.xz * tiling.x;
        const float2 dx = worldDx.xz * tiling.x;
        const float2 dy = worldDy.xz * tiling.x;
        const TerrainTileVariation variation = terrainTileVariation(uv, terrainTint[layer].w > 0.5);
        const float4 layerAlbedo = (sampledAlbedo & (1u << layer)) != 0
            ? albedoSample[layer] : sampleTerrainArray(terrainAlbedo, uv, layer, dx, dy, variation);
        albedo += layerAlbedo.rgb * terrainTint[layer].rgb * amount;
        const float4 normalRoughness = sampleTerrainArray(terrainNormal, uv, layer, dx, dy, variation);
        float3 layerNormal = normalRoughness.xyz * 2.0 - 1.0;
        layerNormal.xy *= tiling.y;
        tangentNormal += layerNormal * amount;
        roughness += normalRoughness.a * tiling.z * amount;
        metallic += tiling.w * amount;
    }

    TerrainSurface surface;
    surface.albedo = albedo;
    // The same tangent frame as the standard material path (terrain tangents run along +X, like its UVs).
    const float3 tangent = normalize(worldTangent.xyz);
    const float3 bitangent = normalize(cross(geometricNormal, tangent)) * worldTangent.w;
    surface.normal = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + geometricNormal * max(tangentNormal.z, 0.001));
    surface.roughness = clamp(roughness, 0.04, 1.0);
    surface.metallic = saturate(metallic);
    return surface;
}
