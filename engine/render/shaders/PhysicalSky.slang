#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]]
cbuffer Parameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]]
Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]]
Sampler2D depthTexture : register(t2, space0);

static const float PI = 3.14159265359;

float hash12(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 hash22(float2 p)
{
    const float value = hash12(p);
    return float2(value, hash12(p + value + 19.19));
}

float starField(float3 direction)
{
    const float2 sphericalUv = float2(atan2(direction.z, direction.x) / (2.0 * PI) + 0.5,
                                      asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
    const float2 grid = sphericalUv * float2(900.0, 450.0);
    const float2 cell = floor(grid);
    const float distanceToStar = length(frac(grid) - hash22(cell));
    const float magnitude = smoothstep(0.996, 1.0, hash12(cell + 71.7));
    return (1.0 - smoothstep(0.015, 0.065, distanceToStar)) * magnitude *
           (0.45 + 0.55 * hash12(cell + 13.1));
}

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id: SV_VertexID)
{
    return fullscreenVertex(id);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    if (depthTexture.Sample(uv).r > 0.000001)
        return scene;

    const float3 worldPosition = reconstructWorldPosition(uv, 1.0, pass.inverseViewProjection, pass.flipY);
    const float3 direction = normalize(worldPosition - pass.cameraPosition.xyz);
    const float3 sunDirection = normalize(pass.parameters[0].xyz);
    const float rayleighStrength = max(pass.parameters[0].w, 0.0);
    const float3 sunColor = max(pass.parameters[1].rgb, 0.0);
    const float mieStrength = max(pass.parameters[1].w, 0.0);
    const float3 moonColor = max(pass.parameters[2].rgb, 0.0);
    const float mieAnisotropy = clamp(pass.parameters[2].w, -0.95, 0.95);
    const float3 groundColor = max(pass.parameters[3].rgb, 0.0);
    const float ozoneStrength = max(pass.parameters[3].w, 0.0);
    const float sunIntensity = max(pass.parameters[4].x, 0.0);
    const float sunRadius = radians(max(pass.parameters[4].y, 0.01));
    const float nightIntensity = max(pass.parameters[4].z, 0.0);
    const float starIntensity = max(pass.parameters[4].w, 0.0);

    const float sunHeight = sunDirection.y;
    const float day = smoothstep(-0.09, 0.035, sunHeight);
    const float night = 1.0 - smoothstep(-0.12, 0.035, sunHeight);
    const float viewAirMass = 1.0 / max(max(direction.y, 0.001) + 0.075, 0.075);
    const float sunAirMass = 1.0 / max(sunHeight + 0.075, 0.04);
    const float3 betaRayleigh = float3(0.028, 0.067, 0.155) * rayleighStrength;
    const float3 betaMie = float3(0.035) * mieStrength;
    const float3 betaExtinction = betaRayleigh + betaMie + float3(0.004, 0.012, 0.002) * ozoneStrength;
    const float3 viewTransmittance = exp(-betaExtinction * viewAirMass);
    const float3 sunTransmittance = exp(-betaExtinction * sunAirMass);
    const float cosine = dot(direction, sunDirection);
    const float rayleighPhase = 3.0 * (1.0 + cosine * cosine) / (16.0 * PI);
    const float mieDenominator = max(1.0 + mieAnisotropy * mieAnisotropy - 2.0 * mieAnisotropy * cosine, 0.0001);
    const float miePhase = (1.0 - mieAnisotropy * mieAnisotropy) / (4.0 * PI * pow(mieDenominator, 1.5));
    float3 sky = (betaRayleigh * rayleighPhase + betaMie * miePhase) *
                 (1.0 - viewTransmittance) / max(betaExtinction, 0.0001) *
                 sunTransmittance * sunColor * sunIntensity * day;
    sky += lerp(float3(0.002, 0.003, 0.009), float3(0.012, 0.022, 0.06),
                pow(saturate(direction.y), 0.35)) *
           nightIntensity * 24.0 * night;
    sky += float3(0.72, 0.82, 1.0) * starField(direction) *
           smoothstep(-0.02, 0.16, direction.y) * night * starIntensity;
    sky += sunColor * sunIntensity *
           (smoothstep(cos(sunRadius * 1.35), cos(sunRadius), cosine) + pow(max(cosine, 0.0), 512.0) * day * 0.08) *
           sunTransmittance;

    const float3 moonDirection = -sunDirection;
    const float moonCosine = dot(direction, moonDirection);
    const float moonRadius = radians(max(pass.parameters[5].y, 0.01));
    const float moonVisibility = night * smoothstep(-0.04, 0.04, moonDirection.y);
    sky += moonColor * max(pass.parameters[5].x, 0.0) * moonVisibility *
           (smoothstep(cos(moonRadius * 1.25), cos(moonRadius), moonCosine) + pow(max(moonCosine, 0.0), 256.0) * 0.08);
    sky = lerp(groundColor * lerp(0.35, 1.0, day), sky, smoothstep(-0.08, 0.015, direction.y));
    return float4(max(sky * max(pass.exposure, 0.0), 0.0), scene.a);
}
