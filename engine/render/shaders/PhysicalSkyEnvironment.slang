// Shared by opaque surfaces and water. Requires physicalSkyParameters, physicalSkySettings and PI.
// Evaluate the physical sky as a direction-addressed environment. This is the
// shader-side equivalent of sampling a generated sky cubemap and avoids a
// frame of latency when atmospheric settings or the sun direction change.
float3 samplePhysicalSkyEnvironment(float3 direction, float sunVisibility = 1.0)
{
    direction = normalize(direction);
    const float3 sunDirection = normalize(physicalSkyParameters[0].xyz);
    const float rayleighStrength = max(physicalSkyParameters[0].w, 0.0);
    const float3 sunColor = max(physicalSkyParameters[1].rgb, 0.0);
    const float mieStrength = max(physicalSkyParameters[1].w, 0.0);
    const float3 moonColor = max(physicalSkyParameters[2].rgb, 0.0);
    const float mieAnisotropy = clamp(physicalSkyParameters[2].w, -0.95, 0.95);
    const float3 groundColor = max(physicalSkyParameters[3].rgb, 0.0);
    const float ozoneStrength = max(physicalSkyParameters[3].w, 0.0);
    const float sunIntensity = max(physicalSkyParameters[4].x, 0.0);
    const float sunRadius = radians(max(physicalSkyParameters[4].y, 0.01));
    const float nightIntensity = max(physicalSkyParameters[4].z, 0.0);
    const float sunHeight = sunDirection.y;
    const float day = smoothstep(-0.09, 0.035, sunHeight);
    const float night = 1.0 - smoothstep(-0.31, -0.04, sunHeight);
    const float twilight = smoothstep(-0.31, -0.02, sunHeight) * (1.0 - smoothstep(0.02, 0.22, sunHeight));
    const float viewAirMass = 1.0 / max(max(direction.y, 0.001) + 0.075, 0.075);
    const float sunAirMass = 1.0 / max(sunHeight + 0.075, 0.04);
    const float3 betaRayleigh = float3(0.028, 0.067, 0.155) * rayleighStrength;
    const float3 betaMie = float3(0.035) * mieStrength;
    const float3 betaExtinction = betaRayleigh + betaMie +
                                  float3(0.004, 0.012, 0.002) * ozoneStrength;
    const float3 viewTransmittance = exp(-betaExtinction * viewAirMass);
    const float3 sunTransmittance = exp(-betaExtinction * sunAirMass);
    const float cosine = dot(direction, sunDirection);
    const float rayleighPhase = 3.0 * (1.0 + cosine * cosine) / (16.0 * PI);
    const float mieDenominator = max(1.0 + mieAnisotropy * mieAnisotropy -
                                     2.0 * mieAnisotropy * cosine, 0.0001);
    const float miePhase = (1.0 - mieAnisotropy * mieAnisotropy) /
                           (4.0 * PI * pow(mieDenominator, 1.5));
    float3 sky = (betaRayleigh * rayleighPhase + betaMie * miePhase) *
                 (1.0 - viewTransmittance) / max(betaExtinction, 0.0001) *
                 sunTransmittance * sunColor * sunIntensity * day;
    const float horizon = pow(1.0 - saturate(direction.y), 5.0);
    const float sunsetAlignment = pow(max(cosine, 0.0), 12.0);
    sky += float3(1.0, 0.12, 0.018) * twilight * twilight * horizon *
           (0.02 + sunsetAlignment * 0.16) * sunIntensity;
    sky += lerp(float3(0.002, 0.003, 0.009), float3(0.012, 0.022, 0.06),
                pow(saturate(direction.y), 0.35)) * nightIntensity * night;
    sky += sunColor * sunIntensity * sunVisibility *
           (smoothstep(cos(sunRadius * 1.35), cos(sunRadius), cosine) * smoothstep(-0.08, 0.02, sunHeight) +
            pow(max(cosine, 0.0), 512.0) * day * 0.08) * sunTransmittance;
    const float3 moonDirection = -sunDirection;
    const float moonCosine = dot(direction, moonDirection);
    const float moonRadius = radians(max(physicalSkyParameters[5].y, 0.01));
    const float moonVisibility = night * smoothstep(-0.04, 0.04, moonDirection.y);
    sky += moonColor * max(physicalSkyParameters[5].x, 0.0) * moonVisibility *
           (smoothstep(cos(moonRadius * 1.25), cos(moonRadius), moonCosine) +
            pow(max(moonCosine, 0.0), 256.0) * 0.08);
    sky = lerp(groundColor * lerp(nightIntensity * 0.1, 1.0, day), sky,
               smoothstep(-0.08, 0.015, direction.y));
    return max(sky * physicalSkySettings.y, 0.0);
}

static const float skyQuadratureCorner = 0.57735026919;
static const float3 skyQuadratureDirections[14] = {
    float3(1,0,0), float3(-1,0,0), float3(0,1,0),
    float3(0,-1,0), float3(0,0,1), float3(0,0,-1),
    float3(1,1,1) * skyQuadratureCorner, float3(1,1,-1) * skyQuadratureCorner,
    float3(1,-1,1) * skyQuadratureCorner, float3(1,-1,-1) * skyQuadratureCorner,
    float3(-1,1,1) * skyQuadratureCorner, float3(-1,1,-1) * skyQuadratureCorner,
    float3(-1,-1,1) * skyQuadratureCorner, float3(-1,-1,-1) * skyQuadratureCorner
};

float3 samplePhysicalSkyIrradiance(float3 normal)
{
    // Integrate a fixed world-space quadrature, rather than rotating five
    // radiance lookups with the receiver normal. Rotating sparse lookups sweeps
    // sharp horizon/Mie features over curved diffuse surfaces like reflections
    // and introduces a seam where the tangent-frame reference axis changes.
    // The 14-point sphere rule uses axis weights 1/15 and corner weights 3/40.
    float3 irradiance = 0.0;
    float weightSum = 0.0;
    for (int tap = 0; tap < 14; ++tap)
    {
        const float weight = max(dot(normal, skyQuadratureDirections[tap]), 0.0) *
                             (tap < 6 ? 1.0 / 15.0 : 3.0 / 40.0);
        // Direct lighting owns the sun disc and its shadow visibility.
#ifdef PLUTO_SKY_QUADRATURE_TEXTURE
        // The opposite hemisphere contributes exactly zero. Keep the reference
        // path intact, and avoid texture fetches for zero-weight cached samples.
        if (physicalSkySettings.w > 0.5 && weight == 0.0) continue;
        const float3 radiance = physicalSkySettings.w > 0.5
            ? skyQuadratureTexture.SampleLevel(float2((float(tap) + 0.5) / 14.0, 0.5), 0.0).rgb
            : samplePhysicalSkyEnvironment(skyQuadratureDirections[tap], 0.0);
#else
        const float3 radiance = samplePhysicalSkyEnvironment(skyQuadratureDirections[tap], 0.0);
#endif
        irradiance += radiance * weight;
        weightSum += weight;
    }
    // Preserve the existing cosine-weighted mean-radiance convention, including
    // constant environments, independently of quadrature orientation.
    return irradiance / max(weightSum, 0.000001);
}

