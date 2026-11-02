// Rectangle lights with linearly transformed cosines (Heitz, Dupuy, Hill and Neubelt, "Real-Time Polygonal-Light
// Shading with Linearly Transformed Cosines", 2016). A cosine lobe transformed by a 3 x 3 matrix approximates the GGX
// BRDF at each roughness and view angle; its integral over a polygon is the clamped-cosine integral of the polygon
// transformed by the inverse matrix, which has a closed form. Diffuse uses the untransformed cosine: the exact form
// factor. The fitted inverse matrices and the BRDF's albedo terms come from tools/FitLtcTables.py (LtcTables.inl),
// uploaded side by side in one texture: matrices in the left half, (norm, fresnel) in the right.
//
// Edge integrals use Hill and Heitz's rational fit of theta / sin(theta) ("Real-Time Area Lighting: a Journey from
// Research to Production", 2016), free of acos; polygons are clipped to the shading point's horizon first.
#ifndef PLUTO_LTC_AREA_LIGHTS
#define PLUTO_LTC_AREA_LIGHTS

#include "SurfaceLayers.slang"

[[vk::binding(8, 0)]] Sampler2D ltcTable : register(t8, space0);
static const float LtcTableSize = 64.0;

struct LtcLobe
{
    float3x3 inverse;  // rows; maps directions in the (view tangent, bitangent, normal) frame to the cosine's
    float2 amplitude;  // BRDF albedo without Fresnel, and weighted by (1 - v.h)^5
};

LtcLobe ltcLobe(float roughness, float ndotv)
{
    const float2 coordinate = float2(saturate(roughness), sqrt(saturate(1.0 - ndotv))) *
                              ((LtcTableSize - 1.0) / LtcTableSize) + 0.5 / LtcTableSize;
    const float4 matrix = ltcTable.SampleLevel(float2(coordinate.x * 0.5, coordinate.y), 0.0);
    const float4 amplitude = ltcTable.SampleLevel(float2(coordinate.x * 0.5 + 0.5, coordinate.y), 0.0);
    LtcLobe lobe;
    lobe.inverse = float3x3(matrix.x, 0.0, matrix.y,
                            0.0, 1.0, 0.0,
                            matrix.z, 0.0, matrix.w);
    lobe.amplitude = amplitude.xy;
    return lobe;
}

// The vector form factor of the arc from v1 to v2 (unit vectors): cross(v1, v2) theta / sin(theta) / (2 pi).
float3 ltcEdge(float3 v1, float3 v2)
{
    const float x = dot(v1, v2);
    const float y = abs(x);
    const float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    const float b = 3.4175940 + (4.1616724 + y) * y;
    const float v = a / b;
    const float thetaSinTheta = x > 0.0 ? v : 0.5 * rsqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * thetaSinTheta;
}

// Clips the quad L[0..3] to the upper hemisphere (z >= 0), leaving `count` vertices (0, 3, 4 or 5) in L with the
// first repeated after the last, so consecutive pairs are the clipped polygon's edges.
void ltcClipQuadToHorizon(inout float3 L[5], out int count)
{
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;
    count = 0;
    if (config == 1) // V1 clip V2 V3 V4
    {
        count = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2) // V2 clip V1 V3 V4
    {
        count = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3) // V1 V2 clip V3 V4
    {
        count = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4) // V3 clip V1 V2 V4
    {
        count = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 6) // V2 V3 clip V1 V4
    {
        count = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7) // V1 V2 V3 clip V4
    {
        count = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8) // V4 clip V1 V2 V3
    {
        count = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = L[3];
    }
    else if (config == 9) // V1 V4 clip V2 V3
    {
        count = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 11) // V1 V2 V4 clip V3
    {
        count = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12) // V3 V4 clip V1 V2
    {
        count = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13) // V1 V3 V4 clip V2
    {
        count = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14) // V2 V3 V4 clip V1
    {
        count = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else if (config == 15) // V1 V2 V3 V4
        count = 4;
    // Configurations 5 and 10 (opposite corners above) cannot occur for a planar quad; 0 is fully below.
    if (count == 3) L[3] = L[0];
    if (count == 4) L[4] = L[0];
}

// The share of the transformed cosine lobe that the quad `corners` (relative to the shading point, wound
// counter-clockwise as seen from its emitting side) covers: 0 to 1. One-sided: seen from behind it covers nothing.
float ltcQuadIntegral(float3 normal, float3 view, float3x3 inverse, float3 corners[4])
{
    float3 tangent = view - normal * dot(view, normal);
    const float tangentLength = dot(tangent, tangent);
    tangent = tangentLength > 1e-8
        ? tangent * rsqrt(tangentLength)
        : normalize(abs(normal.z) < 0.999 ? cross(normal, float3(0.0, 0.0, 1.0)) : cross(normal, float3(1.0, 0.0, 0.0)));
    const float3 bitangent = cross(normal, tangent);
    float3 L[5];
    [ForceUnroll] for (int corner = 0; corner < 4; ++corner)
    {
        const float3 local = float3(dot(corners[corner], tangent), dot(corners[corner], bitangent), dot(corners[corner], normal));
        L[corner] = mul(inverse, local);
    }
    L[4] = L[0];
    int count;
    ltcClipQuadToHorizon(L, count);
    if (count == 0) return 0.0;
    [ForceUnroll] for (int vertex = 0; vertex < 5; ++vertex)
        L[vertex] = normalize(L[vertex]);
    float sum = ltcEdge(L[0], L[1]).z + ltcEdge(L[1], L[2]).z + ltcEdge(L[2], L[3]).z;
    if (count >= 4) sum += ltcEdge(L[3], L[4]).z;
    if (count == 5) sum += ltcEdge(L[4], L[0]).z;
    return max(sum, 0.0); // ltcEdge's fit already includes the polygon formula's 1 / (2 pi)
}

// A rectangle light's reflected diffuse and specular radiance per unit of its radiance, for the surface and its
// layers. `corners` are relative to the shading point (ltcQuadIntegral's winding).
void ltcRectLight(BrdfSurface surface, SurfaceLayers layers, float3 corners[4], out float3 diffuse, out float3 specular)
{
    const float3x3 identity = float3x3(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    const float formFactor = ltcQuadIntegral(surface.normal, surface.view, identity, corners);
    diffuse = surface.diffuseColor * (1.0 - surface.f0) * formFactor;
    specular = float3(0.0);
    // Back-facing or below the horizon: the transformed lobes (positive determinants) see nothing either.
    if (formFactor <= 0.0) return;
    const LtcLobe lobe = ltcLobe(surface.roughness, surface.ndotv);
    specular = ltcQuadIntegral(surface.normal, surface.view, lobe.inverse, corners) *
               (surface.f0 * lobe.amplitude.x + (1.0 - surface.f0) * lobe.amplitude.y) * surface.energyCompensation;
    if (layers.sheenActive)
    {
        // Sheen reflects the light it receives like a diffuse surface of albedo E (as sheenEnvironmentResponse).
        diffuse *= layers.sheenScale;
        specular = specular * layers.sheenScale + layers.sheenColor * formFactor *
                   saturate(sheenDirectionalAlbedo(surface.ndotv, sqrt(layers.sheenAlpha)));
    }
    if (layers.clearCoatActive)
    {
        const LtcLobe coat = ltcLobe(layers.clearCoatRoughness, surface.ndotv);
        diffuse *= 1.0 - layers.viewCoatFresnel;
        specular = specular * (1.0 - layers.viewCoatFresnel) + layers.clearCoat *
                   ltcQuadIntegral(surface.normal, surface.view, coat.inverse, corners) *
                   (0.04 * coat.amplitude.x + 0.96 * coat.amplitude.y);
    }
}

#endif
