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
// Two switches (terrainMapping) let other surfaces share the layers:
// - weights: the weight maps over the terrain's UVs, or vertex layers (terrain volume chunks; no terrain UVs, no macro
//   albedo): uv.x packs the triangle's three layers (a + 256 b + 65536 c), uv.y and uv2.x weigh a and b (c takes the
//   rest), and uv2.y is the vertex's occlusion (scene::volume::VertexLayerEncoding).
// - projection: top-down (x and z), or triplanar (each map projected along the three axes and blended by the
//   normal), for cave walls, overhangs and cliffs a top-down projection would smear.
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
    float4 terrainMapping;                   // x weights (0 maps, 1 vertex layer), y projection (0 top-down, 1 triplanar),
                                             // z triplanar sharpness
};

struct TerrainSurface
{
    float3 albedo;
    float3 normal; // world space
    float roughness;
    float metallic;
    float occlusion; // sky visibility: darkens ambient light only, not direct light (vertex layers carry one; otherwise 1)
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

// ---- one layer --------------------------------------------------------------------------------------------------
// A layer's maps at a point, projected top-down or triplanar. Normals come back in world space (unnormalised), so
// layers blend the same whatever their projection.

struct TerrainShadingFrame
{
    float3 position; // world
    float3 dx, dy;   // world-space derivatives of the position
    float3 normal;   // geometric, normalised
    float3 tangent, bitangent;
};

bool terrainVertexLayers() { return terrainMapping.x > 0.5; }
bool terrainTriplanar() { return terrainMapping.y > 0.5; }

// The three projections' weights: the normal's axes, sharpened so only steep transitions blend.
float3 terrainTriplanarWeights(float3 normal)
{
    const float3 weights = pow(abs(normal), max(terrainMapping.z, 1.0));
    return weights / max(weights.x + weights.y + weights.z, 1.0e-4);
}

float4 sampleTerrainPlane(Sampler2DArray map, uint layer, float2 position, float2 dx, float2 dy)
{
    const float scale = terrainTiling[layer].x;
    const float2 uv = position * scale;
    return sampleTerrainArray(map, uv, layer, dx * scale, dy * scale, terrainTileVariation(uv, terrainTint[layer].w > 0.5));
}

// rgb colour, a height.
float4 sampleTerrainLayerAlbedo(uint layer, TerrainShadingFrame frame)
{
    if (!terrainTriplanar())
        return sampleTerrainPlane(terrainAlbedo, layer, frame.position.xz, frame.dx.xz, frame.dy.xz);
    const float3 weights = terrainTriplanarWeights(frame.normal);
    float4 result = 0.0;
    if (weights.x > kTerrainMinimumWeight)
        result += sampleTerrainPlane(terrainAlbedo, layer, frame.position.zy, frame.dx.zy, frame.dy.zy) * weights.x;
    if (weights.y > kTerrainMinimumWeight)
        result += sampleTerrainPlane(terrainAlbedo, layer, frame.position.xz, frame.dx.xz, frame.dy.xz) * weights.y;
    if (weights.z > kTerrainMinimumWeight)
        result += sampleTerrainPlane(terrainAlbedo, layer, frame.position.xy, frame.dx.xy, frame.dy.xy) * weights.z;
    return result;
}

float3 terrainTangentNormal(float4 normalRoughness, uint layer)
{
    float3 tangentNormal = normalRoughness.xyz * 2.0 - 1.0;
    tangentNormal.xy *= terrainTiling[layer].y;
    return tangentNormal;
}

// xyz world normal (unnormalised), w roughness (before the layer's multiplier).
float4 sampleTerrainLayerNormal(uint layer, TerrainShadingFrame frame)
{
    if (!terrainTriplanar())
    {
        const float4 normalRoughness = sampleTerrainPlane(terrainNormal, layer, frame.position.xz, frame.dx.xz, frame.dy.xz);
        const float3 tangentNormal = terrainTangentNormal(normalRoughness, layer);
        return float4(frame.tangent * tangentNormal.x + frame.bitangent * tangentNormal.y +
                      frame.normal * max(tangentNormal.z, 0.001), normalRoughness.a);
    }
    // Whiteout blend (Golus, "Normal Mapping for a Triplanar Shader"): each projection's tangent normal is added to
    // the geometric normal swizzled into that projection's plane, then swizzled back.
    const float3 weights = terrainTriplanarWeights(frame.normal);
    const float3 n = frame.normal;
    float3 normal = 0.0;
    float roughness = 0.0;
    if (weights.x > kTerrainMinimumWeight)
    {
        const float4 sample = sampleTerrainPlane(terrainNormal, layer, frame.position.zy, frame.dx.zy, frame.dy.zy);
        const float3 t = terrainTangentNormal(sample, layer);
        normal += float3(abs(t.z) * n.x, t.y + n.y, t.x + n.z) * weights.x;
        roughness += sample.a * weights.x;
    }
    if (weights.y > kTerrainMinimumWeight)
    {
        const float4 sample = sampleTerrainPlane(terrainNormal, layer, frame.position.xz, frame.dx.xz, frame.dy.xz);
        const float3 t = terrainTangentNormal(sample, layer);
        normal += float3(t.x + n.x, abs(t.z) * n.y, t.y + n.z) * weights.y;
        roughness += sample.a * weights.y;
    }
    if (weights.z > kTerrainMinimumWeight)
    {
        const float4 sample = sampleTerrainPlane(terrainNormal, layer, frame.position.xy, frame.dx.xy, frame.dy.xy);
        const float3 t = terrainTangentNormal(sample, layer);
        normal += float3(t.x + n.x, t.y + n.y, abs(t.z) * n.z) * weights.z;
        roughness += sample.a * weights.z;
    }
    return float4(normal, roughness);
}

// ---- blending -------------------------------------------------------------------------------------------------

// How much of the layers shows at a distance from the camera: 1 within the detail distance, fading to 0 (the
// macro albedo alone) over the fade band beyond it. Surfaces with vertex layers have no macro albedo.
float terrainDetailAmount(float cameraDistance)
{
    const float limit = terrainParameters.z;
    return limit <= 0.0 || terrainVertexLayers() ? 1.0 : saturate((limit - cameraDistance) / max(terrainParameters.w, 0.001) + 1.0);
}

TerrainSurface terrainLayerSurface(float3 albedo, float3 normal, float roughness, float metallic)
{
    TerrainSurface surface;
    surface.albedo = albedo;
    surface.normal = normalize(normal);
    surface.roughness = clamp(roughness, 0.04, 1.0);
    surface.metallic = saturate(metallic);
    surface.occlusion = 1.0;
    return surface;
}

// Vertex layers: up to three layers named by the triangle, weighed per vertex, so seams between layers blend.
TerrainSurface sampleVertexLayers(float2 terrainUv, float2 terrainUv2, TerrainShadingFrame frame, uint count)
{
    const uint packed = uint(max(terrainUv.x, 0.0) + 0.5);
    const uint last = max(count, 1u) - 1u;
    uint layers[3] = { min(packed & 255u, last), min((packed >> 8) & 255u, last), min((packed >> 16) & 255u, last) };
    float weights[3] = { saturate(terrainUv.y), saturate(terrainUv2.x), 0.0 };
    weights[2] = saturate(1.0 - weights[0] - weights[1]);
    // The same layer in two slots counts once.
    [unroll] for (uint slot = 1; slot < 3; ++slot)
        [unroll] for (uint earlier = 0; earlier < slot; ++earlier)
            if (layers[slot] == layers[earlier] && weights[slot] > 0.0)
            {
                weights[earlier] += weights[slot];
                weights[slot] = 0.0;
            }

    float3 albedo = 0.0;
    float3 normal = 0.0;
    float roughness = 0.0;
    float metallic = 0.0;
    float total = 0.0;
    [unroll] for (uint slot = 0; slot < 3; ++slot)
    {
        if (weights[slot] <= kTerrainMinimumWeight)
            continue;
        const uint layer = layers[slot];
        const float4 normalRoughness = sampleTerrainLayerNormal(layer, frame);
        albedo += sampleTerrainLayerAlbedo(layer, frame).rgb * terrainTint[layer].rgb * weights[slot];
        normal += normalRoughness.xyz * weights[slot];
        roughness += normalRoughness.a * terrainTiling[layer].z * weights[slot];
        metallic += terrainTiling[layer].w * weights[slot];
        total += weights[slot];
    }
    if (total <= 0.0)
    {
        const float4 normalRoughness = sampleTerrainLayerNormal(layers[0], frame);
        albedo = sampleTerrainLayerAlbedo(layers[0], frame).rgb * terrainTint[layers[0]].rgb;
        normal = normalRoughness.xyz;
        roughness = normalRoughness.a * terrainTiling[layers[0]].z;
        metallic = terrainTiling[layers[0]].w;
        total = 1.0;
    }
    TerrainSurface surface = terrainLayerSurface(albedo / total, normal, roughness / total, metallic / total);
    surface.occlusion = saturate(terrainUv2.y);
    return surface;
}

TerrainSurface sampleTerrainLayers(float2 terrainUv, float2 terrainUv2, float3 worldPosition, float3 worldDx, float3 worldDy,
                                   float3 geometricNormal, float4 worldTangent)
{
    const uint count = min(uint(terrainParameters.x + 0.5), kTerrainMaxLayers);
    const float heightBlend = terrainParameters.y;

    TerrainShadingFrame frame;
    frame.position = worldPosition;
    frame.dx = worldDx;
    frame.dy = worldDy;
    frame.normal = geometricNormal;
    // The same tangent frame as the standard material path (terrain tangents run along +X, like its UVs).
    frame.tangent = normalize(worldTangent.xyz);
    frame.bitangent = normalize(cross(geometricNormal, frame.tangent)) * worldTangent.w;

    if (terrainVertexLayers())
        return sampleVertexLayers(terrainUv, terrainUv2, frame, count);

    // Fast path: one layer. Most of a painted terrain lies inside one layer (open dust, rock faces), where no
    // blending is needed.
    uint only = kTerrainMaxLayers;
    float4 weights0 = 0.0, weights1 = 0.0;
    {
        // Weights change slowly across the terrain; the base level avoids blurring painted edges at a distance.
        weights0 = terrainWeights0.SampleLevel(terrainUv, 0.0);
        weights1 = terrainWeights1.SampleLevel(terrainUv, 0.0);
        [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
            if (layer < count && terrainWeight(weights0, weights1, layer) >= 1.0 - kTerrainMinimumWeight)
                only = layer;
    }
    if (only < kTerrainMaxLayers || count <= 1)
    {
        const uint layer = only < kTerrainMaxLayers ? only : 0;
        const float4 normalRoughness = sampleTerrainLayerNormal(layer, frame);
        return terrainLayerSurface(sampleTerrainLayerAlbedo(layer, frame).rgb * terrainTint[layer].rgb, normalRoughness.xyz,
                                   normalRoughness.a * terrainTiling[layer].z, terrainTiling[layer].w);
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
            albedoSample[layer] = sampleTerrainLayerAlbedo(layer, frame);
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
    float3 normal = 0.0;
    float roughness = 0.0;
    float metallic = 0.0;
    [unroll] for (uint layer = 0; layer < kTerrainMaxLayers; ++layer)
    {
        if ((shaded & (1u << layer)) == 0)
            continue;
        const float amount = blend[layer] / total;
        const float4 layerAlbedo = (sampledAlbedo & (1u << layer)) != 0 ? albedoSample[layer] : sampleTerrainLayerAlbedo(layer, frame);
        albedo += layerAlbedo.rgb * terrainTint[layer].rgb * amount;
        const float4 normalRoughness = sampleTerrainLayerNormal(layer, frame);
        normal += normalRoughness.xyz * amount;
        roughness += normalRoughness.a * terrainTiling[layer].z * amount;
        metallic += terrainTiling[layer].w * amount;
    }
    return terrainLayerSurface(albedo, normal, roughness, metallic);
}
