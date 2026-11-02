// Ambient occlusion terms shared by material occlusion maps (BasicLit.slang) and screen-space AO (SSAOCommon.slang),
// so both occlude diffuse light, reflections and direct light the same way. AO here is the visible share of the
// hemisphere: 1 open, 0 fully occluded.
#ifndef PLUTO_AMBIENT_OCCLUSION
#define PLUTO_AMBIENT_OCCLUSION

// The share of a GGX reflection lobe that ambient occlusion `ao` leaves visible, from the cone of directions AO
// implies (Lagarde and de Rousiers, "Moving Frostbite to PBR", 2014). Smooth lobes are occluded only where AO is low
// and the view is grazing; rough lobes follow AO.
float specularOcclusion(float ao, float ndotv, float roughness)
{
    return saturate(pow(ndotv + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// Occlusion of diffuse ambient light with the interreflections a cavity keeps: light bouncing inside it lifts
// bright albedos (Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion", 2016, the
// cubic fit to ground truth). Never darker than `ao` itself.
float3 multiBounceOcclusion(float ao, float3 albedo)
{
    const float3 a = 2.0404 * albedo - 0.3324;
    const float3 b = -4.7951 * albedo + 0.6417;
    const float3 c = 2.7552 * albedo + 0.6903;
    return max(float3(ao), ((ao * a + b) * ao + c) * ao);
}

// Direct-light visibility inside a cavity whose opening `ao` describes: the cavity is treated as a cone of aperture
// 2 ao^2 around the normal, so light from within the cone is unshadowed and light from outside it fades out
// (Chan, "Material Advances in Call of Duty: WWII", 2018). `ndotl` is the shading normal's cosine to the light.
float microShadowing(float ao, float ndotl)
{
    return saturate(abs(ndotl) + 2.0 * ao * ao - 1.0);
}

#endif
