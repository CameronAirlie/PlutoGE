// Lightmaps (docs/BAKED_LIGHTING.md): static draws sample baked light from one atlas through their secondary UVs.
// The atlas holds render::LightmapEncoding::IrradianceShadowMask: rgb is cosine-weighted mean radiance (irradiance /
// pi, the convention every ambient layer multiplies by the diffuse response), a is the directional light's static
// shadow mask. Requires SceneLightingParameters.slang (bakedLighting).

// BasicRenderer::LightmapAtlasSlot / LightmapAtlasBinding. Glass (blended, never lightmapped) leaves it out: its
// pipeline is at OpenGL's fragment sampler limit.
#ifndef PLUTO_GLASS
[[vk::binding(39, 0)]] Sampler2D lightmapAtlas : register(t39, space0);
#endif

// The indirect target's alpha for lightmapped surfaces (DiffuseResponse.slang lists every code): baked bounce light
// that DDGI and SSGI keep (they replace only at 0.5 and above) and screen-space AO still darkens.
static const float BakedIndirectAlpha = 0.375;

// The atlas coordinate of a vertex: uv2 through the draw's rect (BasicDraw::lightmap, xy scale and zw offset). z is 1
// for lightmapped draws, so it stays 1 across their triangles; 0 otherwise.
float3 lightmapCoordinate(float2 uv2, float4 scaleOffset)
{
    return scaleOffset.x > 0.0 ? float3(uv2 * scaleOffset.xy + scaleOffset.zw, 1.0) : float3(0.0, 0.0, 0.0);
}

struct LightmapSample
{
    bool present;
    float3 radiance;  // mean radiance from baked sources
    float shadowMask; // 1 lit, 0 in static shadow; 1 without a mask
};

LightmapSample sampleLightmap(float3 coordinate)
{
    LightmapSample result;
    result.present = false;
    result.radiance = float3(0.0, 0.0, 0.0);
    result.shadowMask = 1.0;
#ifndef PLUTO_GLASS
    if (bakedLighting.x < 0.5 || coordinate.z < 0.5)
        return result;
    // The atlas has no mips: its charts are padded for bilinear reads at full resolution only.
    const float4 texel = lightmapAtlas.SampleLevel(coordinate.xy, 0.0);
    result.present = true;
    result.radiance = max(texel.rgb, float3(0.0, 0.0, 0.0));
    result.shadowMask = bakedLighting.z > 0.5 ? saturate(texel.a) : 1.0;
#endif
    return result;
}

// The directional light is Baked: lightmaps and ambient volumes hold all of its light, so surfaces add none of it.
bool directionalLightBaked()
{
    return bakedLighting.x > 0.5 && bakedLighting.y > 1.5;
}
