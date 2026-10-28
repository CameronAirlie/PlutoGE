// DDGI reflection occlusion: the probe volume's sky visibility applied to the environment reflection the geometry
// pass wrote (DDGIApply.slang). Requires DDGICommon.slang with PLUTO_DDGI_SAMPLING.
//
// Where SSR finds no hit, surfaces reflect the sky (or a reflection capture) whether or not they can see it, so
// glossy floors indoors show the sky. The probes know better: their rays that leave the scene reach the sky, and
// the visibility blend keeps that share per direction (DDGIProbeUpdate.slang). The sky share of a pixel's
// environment reflection (the environment specular target's alpha) is scaled by that visibility along the
// reflection, and what the sky no longer supplies comes from the probes' light in the same direction.
//
// Approximations, and where to improve them:
//  - The replacement light is the probes' irradiance along the reflection: low frequency, and including whatever sky
//    the cosine lobe sees. A radiance atlas of hits only, or ray-traced radiance, would sharpen and separate it.
//  - Visibility is the probes', interpolated to the surface, so its detail is that of the probe spacing.
//  - Where reflection captures blend with the sky, occlusion scales the whole reflection rather than its sky part.
#ifndef PLUTO_DDGI_REFLECTION
#define PLUTO_DDGI_REFLECTION

#include "EnvironmentBRDF.slang"

bool ddgiReflectionOcclusionEnabled() { return ddgiReflection.x > 0.0; }

// The direction a reflection lobe's sky visibility is read along. Rough lobes lean towards the normal (GGX's
// dominant direction, after Lagarde and de Rousiers, "Moving Frostbite to PBR"). The lookup also stays above the
// receiver's tangent plane by the visibility lobe's width: the probes see the receiver's own surface below that
// plane, which cannot occlude the reflection.
float3 ddgiReflectionLookup(float3 reflection, float3 normal, float roughness)
{
    const float smoothness = 1.0 - saturate(roughness);
    const float3 dominant = normalize(lerp(normal, reflection, smoothness * (sqrt(smoothness) + roughness)));
    const float elevation = dot(dominant, normal);
    const float minimum = ddgiReflection.y;
    return elevation >= minimum ? dominant : normalize(dominant + normal * (minimum - elevation));
}

// The environment reflection `environment` (rgb radiance times the split-sum response, a its sky share) with its
// sky share occluded by the probes' visibility. `coverage` is the probes' (ddgiSample); `response` the surface's
// split-sum specular response. `occlusion` returns the share of the reflection replaced.
float3 ddgiOccludeEnvironmentSpecular(float4 environment, DdgiSample probes, float coverage, float3 response,
                                      out float occlusion)
{
    occlusion = saturate(ddgiReflection.x * saturate(environment.a) * coverage * (1.0 - saturate(probes.skyVisibility)));
    return lerp(environment.rgb, probes.radiance * ddgiApply.x * response, occlusion);
}

#endif
