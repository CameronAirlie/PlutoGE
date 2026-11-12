// Rain on surfaces (WeatherComponent wetness, frame parameter surfaceWeather.x). Water fills a porous surface's pores
// and darkens it, and a film of water makes any surface glossier. Both follow how much a surface faces the sky:
// full on ground and roofs, a little on walls (run-off), none on undersides. Metals and already smooth surfaces barely
// darken, because they have no pores to fill. Never makes a surface rougher than it was dry.
//
// Known limit: wetness is not occluded, so ground under a roof or a dense canopy is as wet as open ground.

// How much a surface with this geometric normal faces the rain: 1 up, about 0.35 vertical, 0 facing down.
float surfaceRainExposure(float3 geometricNormal)
{
    return saturate(normalize(geometricNormal).y * 0.65 + 0.35);
}

void applySurfaceWetness(float wetness, float3 geometricNormal, inout float3 albedo, inout float roughness, float metallic)
{
    if (wetness <= 0.0)
        return;
    const float wet = saturate(wetness) * surfaceRainExposure(geometricNormal);
    // Rough, non-metallic surfaces are the porous ones: soil, bark, rock, wood.
    const float porosity = saturate((roughness - 0.25) / 0.6) * (1.0 - metallic);
    albedo *= lerp(1.0, 0.45, wet * porosity);
    roughness = lerp(roughness, min(roughness, 0.15), wet * 0.85);
}
