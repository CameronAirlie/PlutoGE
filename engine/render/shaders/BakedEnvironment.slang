// Baked environment reflections: reflection captures and how the baked ambient volumes adjust them
// (docs/BAKED_ENVIRONMENT_LIGHTING.md). Shared by the forward surface shader (BasicLit.slang) and the deferred probe
// resolve (DiffuseProbeResolve.slang), so both apply identical terms. Requires SceneLightingParameters.slang and
// AmbientProbeSampling.slang.
//
//  - Sky reflection occlusion: a reflection that would show the sky is scaled by the probes' sky visibility along
//    it, so glossy floors indoors stop mirroring a sky they cannot see.
//  - Capture relighting: a capture is a snapshot from one point at bake time. Scaling its reflection by the volume's
//    irradiance relative to the capture's own (the normalisation of Lazarov, "Getting More Physical in Call of Duty:
//    Black Ops II", 2013) darkens corners the capture point could not see into and makes captures follow the live
//    sky and time of day.
#ifndef PLUTO_BAKED_ENVIRONMENT
#define PLUTO_BAKED_ENVIRONMENT

// Brighter than this, relighting would amplify a capture's noise and its missing detail; a capture made under
// darker lighting than the scene is shown in stays dimmer than it should.
static const float BakedCaptureRelightLimit = 4.0;
// Sky visibility is read at least this far above the receiver's tangent plane: below it the probes see the
// receiver's own surface, which cannot occlude its reflection.
static const float BakedReflectionMinimumElevation = 0.2;

float bakedLuminance(float3 color) { return dot(color, float3(0.2126, 0.7152, 0.0722)); }

// 1 inside capture `index`'s box, fading to 0 at its faces over the blend distance.
float iblWeight(int index, float3 position)
{
    if (iblSettings[index].x < 0.5) return 0;
    const float3 local = position - iblOriginIntensity[index].xyz;
    const float3 edge = min(local, iblSizeBlend[index].xyz - local);
    const float distance = min(edge.x, min(edge.y, edge.z));
    if (distance < 0) return 0;
    return iblSizeBlend[index].w > 0 ? saturate(distance / iblSizeBlend[index].w) : 1;
}

// Capture `index`'s diffuse irradiance, in the probe grid's convention.
float3 iblDiffuse(int index, float3 n)
{
    const float basis[9] = {0.282095, 0.488603*n.y, 0.488603*n.z, 0.488603*n.x,
        1.092548*n.x*n.y, 1.092548*n.y*n.z, 0.315392*(3*n.z*n.z-1),
        1.092548*n.x*n.z, 0.546274*(n.x*n.x-n.y*n.y)};
    float3 irradiance = float3(0);
    for (int coefficient = 0; coefficient < 9; ++coefficient)
        irradiance += iblIrradiance[index * 9 + coefficient].rgb * basis[coefficient];
    return max(irradiance, 0.0) * iblOriginIntensity[index].w;
}

bool iblRelightsFromAmbient(int index) { return iblSettings[index].w > 0.5; }

// The factor scaling capture `index`'s reflection at a receiver whose ambient volume supplies `gridIrradiance`.
float iblRelightFactor(int index, float3 normal, float3 gridIrradiance)
{
    if (!iblRelightsFromAmbient(index)) return 1.0;
    const float captured = bakedLuminance(iblDiffuse(index, normal));
    // A black capture has nothing to relight.
    return captured > 0.0001 ? clamp(bakedLuminance(gridIrradiance) / captured, 0.0, BakedCaptureRelightLimit) : 1.0;
}

// The direction a reflection lobe's sky visibility is read along. Rough lobes lean towards the normal (GGX's
// dominant direction, Lagarde and de Rousiers, "Moving Frostbite to PBR", 2014).
float3 bakedReflectionLookup(float3 reflection, float3 normal, float roughness)
{
    const float smoothness = 1.0 - saturate(roughness);
    const float3 dominant = normalize(lerp(normal, reflection, smoothness * (sqrt(smoothness) + roughness)));
    const float elevation = dot(dominant, normal);
    return elevation >= BakedReflectionMinimumElevation
        ? dominant : normalize(dominant + normal * (BakedReflectionMinimumElevation - elevation));
}

// The share of a sky reflection the volume leaves visible.
float bakedSkyReflectionVisibility(DiffuseGridSample grid, float3 reflection, float3 normal, float roughness)
{
    return grid.coverage > 0 ? evaluateSkyVisibility(grid.skyVisibility, bakedReflectionLookup(reflection, normal, roughness)) : 1.0;
}

#endif
