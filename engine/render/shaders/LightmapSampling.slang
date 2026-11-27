// Lightmaps (docs/BAKED_LIGHTING.md): static draws sample baked light from one atlas through their secondary UVs.
// The atlas holds render::LightmapEncoding::IrradianceShadowMask: rgb is cosine-weighted mean radiance (irradiance /
// pi, the convention every ambient layer multiplies by the diffuse response), a is the directional light's static
// shadow mask. Requires SceneLightingParameters.slang (bakedLighting).

// BasicRenderer::LightmapAtlasSlot / LightmapAtlasBinding (equal: OpenGL uses the binding as the texture unit). Glass (blended, never lightmapped) leaves it out: its
// pipeline is at OpenGL's fragment sampler limit.
#ifndef PLUTO_GLASS
[[vk::binding(42, 0)]] Sampler2D lightmapAtlas : register(t42, space0);
#endif

// The indirect target's alpha for lightmapped surfaces (DiffuseResponse.slang lists every code): baked bounce light
// that DDGI and SSGI keep (they replace only at 0.5 and above) and screen-space AO still darkens.
static const float BakedIndirectAlpha = 0.375;

// The atlas coordinate of a vertex: uv2 through the draw's rect (BasicDraw::lightmap, xy scale and zw offset). z is 1
// for lightmapped draws, so it stays 1 across their triangles; -1 for static meshes without texels
// (LightmapRect::staticReceiver); 0 otherwise.
float3 lightmapCoordinate(float2 uv2, float4 scaleOffset)
{
    if (scaleOffset.x > 0.0)
        return float3(uv2 * scaleOffset.xy + scaleOffset.zw, 1.0);
    return float3(0.0, 0.0, scaleOffset.z < -0.5 ? -1.0 : 0.0);
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

// The Lightmap (mode 1) and Lightmap Coverage (mode 2) debug views (PostProcessDebugView, geometryParameters.y),
// written to the albedo target that DisplayOutput.slang shows. facing (0..1, toward the camera) keeps shapes readable.
float3 lightmapDebugColor(float mode, float3 coordinate, LightmapSample lightmap, float facing)
{
    const float shape = 0.55 + 0.45 * facing;
    if (mode < 1.5)
    {
        if (!lightmap.present)
            return float3(0.035, 0.035, 0.035) * shape;
        // Mean radiance through a simple tone curve (the view has no exposure of its own), gamma-encoded: the display
        // target is UNORM and shows debug values as they are.
        const float3 radiance = lightmap.radiance;
        const float3 mapped = radiance / (1.0 + dot(radiance, float3(0.2126, 0.7152, 0.0722)));
        return pow(max(mapped, float3(0.0, 0.0, 0.0)), float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    }
    float3 tint = float3(0.35, 0.35, 0.37);                       // lit without lightmaps
    if (lightmap.present) tint = float3(0.15, 0.85, 0.25);         // lightmapped
    else if (coordinate.z > 0.5) tint = float3(0.95, 0.80, 0.15);  // has texels; scene lighting is Realtime
    else if (coordinate.z < -0.5) tint = float3(0.95, 0.15, 0.12); // static, left out of the bake
    return tint * shape;
}

// Shadowmask (Shadowmask.h): static geometry's sun shadow is baked, and the realtime shadow holds moving casters only.
bool bakedShadowmask()
{
    return bakedLighting.x > 0.5 && bakedLighting.w > 0.5;
}

// The directional light is Baked: lightmaps and ambient volumes hold all of its light, so surfaces add none of it.
bool directionalLightBaked()
{
    return bakedLighting.x > 0.5 && bakedLighting.y > 1.5;
}
