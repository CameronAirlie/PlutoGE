// Clear coat and sheen: optional layers over the base microfacet BRDF (SurfaceBrdf.slang). BasicLit composes them;
// each is skipped when its factor is zero, so materials without layers shade exactly as before.
//
//  - Clear coat: a thin, smooth dielectric over the base (lacquer, car paint, varnish). An isotropic GGX lobe with
//    F0 = 0.04 (IOR 1.5) and Kelemen's visibility; the base below receives and returns the light the coat transmits,
//    1 - Fc (Filament's model; glTF KHR_materials_clearcoat).
//  - Sheen: back-scattering from fibres standing off the surface (cloth, velvet, dust). The Charlie distribution
//    (Estevez and Kulla, "Production Friendly Microfacet Sheen BRDF", 2017) with Neubelt's visibility; the layers
//    below are scaled by 1 - max(sheen colour) E, E the lobe's directional albedo (glTF KHR_materials_sheen).
//
// Environment light: the coat reflects a second environment sample at its own roughness (coatEnvironmentResponse);
// sheen reflects the diffuse irradiance, so it joins the diffuse response (sheenEnvironmentResponse).
#ifndef PLUTO_SURFACE_LAYERS
#define PLUTO_SURFACE_LAYERS

#include "SurfaceBrdf.slang"

static const float SheenMinimumRoughness = 0.07;

// Directional albedo of the sheen lobe, 16 x 16: rows perceptual roughness from SheenMinimumRoughness to 1, columns
// n.v from 0 to 1. Integrated numerically; exceeds 1 at grazing views, where Neubelt's visibility gains energy.
static const float SheenAlbedoTable[256] = {
    1.8362, 0.8478, 0.3149, 0.0948, 0.0215, 0.0039, 0.0006, 0.0001, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000,
    1.6014, 1.0566, 0.6339, 0.3507, 0.1778, 0.0853, 0.0386, 0.0162, 0.0062, 0.0021, 0.0006, 0.0001, 0.0000, 0.0000, 0.0000, 0.0000,
    1.3395, 0.9987, 0.7005, 0.4694, 0.3015, 0.1894, 0.1160, 0.0686, 0.0388, 0.0207, 0.0103, 0.0046, 0.0018, 0.0005, 0.0001, 0.0000,
    1.1422, 0.9081, 0.6905, 0.5094, 0.3666, 0.2611, 0.1833, 0.1262, 0.0846, 0.0548, 0.0339, 0.0197, 0.0105, 0.0049, 0.0017, 0.0003,
    0.9985, 0.8265, 0.6606, 0.5166, 0.3975, 0.3043, 0.2310, 0.1732, 0.1276, 0.0918, 0.0641, 0.0429, 0.0270, 0.0155, 0.0075, 0.0023,
    0.8923, 0.7594, 0.6279, 0.5106, 0.4106, 0.3294, 0.2630, 0.2081, 0.1627, 0.1251, 0.0940, 0.0685, 0.0478, 0.0312, 0.0182, 0.0083,
    0.8123, 0.7053, 0.5976, 0.4997, 0.4145, 0.3436, 0.2840, 0.2334, 0.1901, 0.1528, 0.1208, 0.0932, 0.0697, 0.0496, 0.0327, 0.0186,
    0.7507, 0.6619, 0.5712, 0.4876, 0.4138, 0.3514, 0.2980, 0.2516, 0.2110, 0.1753, 0.1436, 0.1156, 0.0907, 0.0687, 0.0493, 0.0322,
    0.7025, 0.6267, 0.5486, 0.4758, 0.4108, 0.3553, 0.3072, 0.2648, 0.2271, 0.1933, 0.1628, 0.1352, 0.1101, 0.0873, 0.0665, 0.0476,
    0.6640, 0.5980, 0.5294, 0.4649, 0.4070, 0.3570, 0.3133, 0.2745, 0.2396, 0.2078, 0.1788, 0.1521, 0.1274, 0.1046, 0.0833, 0.0636,
    0.6330, 0.5745, 0.5132, 0.4552, 0.4028, 0.3574, 0.3175, 0.2817, 0.2493, 0.2196, 0.1921, 0.1666, 0.1427, 0.1203, 0.0992, 0.0792,
    0.6077, 0.5549, 0.4994, 0.4466, 0.3987, 0.3571, 0.3203, 0.2872, 0.2570, 0.2292, 0.2033, 0.1790, 0.1561, 0.1344, 0.1138, 0.0940,
    0.5868, 0.5386, 0.4877, 0.4391, 0.3949, 0.3564, 0.3222, 0.2914, 0.2632, 0.2371, 0.2126, 0.1896, 0.1678, 0.1469, 0.1270, 0.1077,
    0.5693, 0.5249, 0.4777, 0.4325, 0.3913, 0.3554, 0.3236, 0.2947, 0.2683, 0.2437, 0.2206, 0.1988, 0.1780, 0.1580, 0.1388, 0.1203,
    0.5546, 0.5132, 0.4691, 0.4268, 0.3881, 0.3544, 0.3245, 0.2973, 0.2724, 0.2492, 0.2273, 0.2066, 0.1869, 0.1678, 0.1495, 0.1316,
    0.5422, 0.5032, 0.4617, 0.4218, 0.3852, 0.3534, 0.3251, 0.2994, 0.2758, 0.2538, 0.2331, 0.2135, 0.1946, 0.1765, 0.1589, 0.1418
};

float sheenDirectionalAlbedo(float ndotv, float roughness)
{
    const float2 coordinate = float2(saturate(ndotv),
                                     saturate((roughness - SheenMinimumRoughness) / (1.0 - SheenMinimumRoughness))) * 15.0;
    const int2 base = min(int2(coordinate), int2(14));
    const float2 fraction = coordinate - float2(base);
    const int row = base.y * 16 + base.x;
    const float lower = lerp(SheenAlbedoTable[row], SheenAlbedoTable[row + 1], fraction.x);
    const float upper = lerp(SheenAlbedoTable[row + 16], SheenAlbedoTable[row + 17], fraction.x);
    return lerp(lower, upper, fraction.y);
}

struct SurfaceLayers
{
    float clearCoat;            // 0 to 1
    float clearCoatRoughness;   // perceptual
    float clearCoatAlphaSquared;
    float3 sheenColor;
    float sheenAlpha;           // sheen roughness squared
    // How much of the light the layers above leave the base layer, for analytic and environment light alike at the
    // view angle: sheen's albedo scaling (each light's coat transmission uses its own angle).
    float sheenScale;
    float viewCoatFresnel;      // the coat's Fresnel at n.v, times clearCoat
    bool clearCoatActive, sheenActive;
};

SurfaceLayers surfaceLayers(BrdfSurface base, float clearCoat, float clearCoatRoughness, float3 sheenColor,
                            float sheenRoughness)
{
    SurfaceLayers layers;
    layers.clearCoat = saturate(clearCoat);
    layers.clearCoatRoughness = clamp(clearCoatRoughness, 0.04, 1.0);
    const float coatAlpha = layers.clearCoatRoughness * layers.clearCoatRoughness;
    layers.clearCoatAlphaSquared = coatAlpha * coatAlpha;
    layers.sheenColor = max(sheenColor, 0.0);
    const float sheenPerceptual = clamp(sheenRoughness, SheenMinimumRoughness, 1.0);
    layers.sheenAlpha = sheenPerceptual * sheenPerceptual;
    layers.clearCoatActive = layers.clearCoat > 0.0;
    layers.sheenActive = any(layers.sheenColor > 0.0);
    layers.sheenScale = layers.sheenActive
        ? saturate(1.0 - max(layers.sheenColor.r, max(layers.sheenColor.g, layers.sheenColor.b)) *
                         saturate(sheenDirectionalAlbedo(base.ndotv, sheenPerceptual)))
        : 1.0;
    layers.viewCoatFresnel = layers.clearCoatActive
        ? layers.clearCoat * (0.04 + 0.96 * pow(1.0 - base.ndotv, 5.0)) : 0.0;
    return layers;
}

float sheenDistribution(float alpha, float ndoth)
{
    const float inverseAlpha = 1.0 / alpha;
    const float sin2h = max(1.0 - ndoth * ndoth, 0.0078125); // 2^-7 keeps half precision away from zero
    return (2.0 + inverseAlpha) * pow(sin2h, inverseAlpha * 0.5) / (2.0 * BrdfPi);
}

float sheenVisibility(float ndotv, float ndotl)
{
    return saturate(1.0 / (4.0 * (ndotl + ndotv - ndotl * ndotv)));
}

// Adds the layers to one light's base reflectance from brdfEvaluate (diffuse and specular, both times n.l): sheen
// over the base, then the coat over both. The layers' own lobes are added to `specular`.
void layersEvaluate(BrdfSurface base, SurfaceLayers layers, BrdfLight light, float ndotl, inout float3 diffuse,
                    inout float3 specular)
{
    if (!layers.sheenActive && !layers.clearCoatActive) return;
    const float3 h = normalize(light.direction + base.view);
    const float ndoth = saturate(dot(base.normal, h));
    const float specularNdotl = saturate(dot(base.normal, light.direction));
    if (layers.sheenActive)
    {
        diffuse *= layers.sheenScale;
        specular = specular * layers.sheenScale + layers.sheenColor *
                   (sheenDistribution(layers.sheenAlpha, ndoth) * sheenVisibility(base.ndotv, specularNdotl) * specularNdotl);
    }
    if (layers.clearCoatActive)
    {
        const float ldoth = saturate(dot(light.direction, h));
        const float coatFresnel = layers.clearCoat * (0.04 + 0.96 * pow(1.0 - ldoth, 5.0));
        const float distribution = brdfDistribution(layers.clearCoatAlphaSquared, ndoth) * light.specularNormalization;
        const float visibility = 0.25 / max(ldoth * ldoth, 0.0001);
        diffuse *= 1.0 - coatFresnel;
        specular = specular * (1.0 - coatFresnel) + distribution * visibility * coatFresnel * specularNdotl;
    }
}

// The scale of the base layer's environment light (diffuse and reflection) under the layers.
float layersEnvironmentBaseScale(SurfaceLayers layers)
{
    return layers.sheenScale * (1.0 - layers.viewCoatFresnel);
}

// What the coat's environment reflection (radiance at its roughness along the reflection) is multiplied by.
float3 coatEnvironmentResponse(SurfaceLayers layers, float ndotv)
{
    return layers.clearCoatActive ? layers.clearCoat * iblSpecularResponse(float3(0.04), layers.clearCoatRoughness, ndotv)
                                  : float3(0.0);
}

// What diffuse irradiance is multiplied by for the sheen's environment light (the coat transmits it as it does the
// base's): its albedo along the view times the mean incident radiance, irradiance / pi.
float3 sheenEnvironmentResponse(SurfaceLayers layers, float ndotv)
{
    if (!layers.sheenActive) return float3(0.0);
    const float roughness = sqrt(layers.sheenAlpha);
    return layers.sheenColor * saturate(sheenDirectionalAlbedo(ndotv, roughness)) * (1.0 - layers.viewCoatFresnel) / BrdfPi;
}

#endif
