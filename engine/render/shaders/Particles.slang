struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float4 data : TEXCOORD1;
    float3 center : TEXCOORD2;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation float4 data : TEXCOORD1;
    nointerpolation float3 centerView : TEXCOORD2;
};
[[vk::binding(0, 0)]] cbuffer ParticleParameters : register(b0, space0)
{
    column_major float4x4 viewProjection;
    column_major float4x4 inverseProjection;
    column_major float4x4 view;
    float4 values[24];
};
[[vk::binding(1, 0)]] Sampler2D uAlbedoTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D uSceneDepth : register(t2, space0);
struct ParticleInstance
{
    float4 centerRotation;
    float4 color;
    float4 data;
};
[[vk::binding(3, 0)]] StructuredBuffer<ParticleInstance> particleInstances : register(t3, space0);
[shader("vertex")]
VertexOutput instancedVertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const float2 corners[4] = {float2(-0.5, -0.5), float2(0.5, -0.5),
                               float2(-0.5, 0.5), float2(0.5, 0.5)};
    const ParticleInstance particle = particleInstances[instanceId];
    const float2 corner = corners[vertexId];
    float sine, cosine;
    sincos(particle.centerRotation.w, sine, cosine);
    const float2 rotated = float2(cosine * corner.x + sine * corner.y,
                                  -sine * corner.x + cosine * corner.y);
    // The view rotation maps world to camera; its rows are the billboard basis.
    const float3 offset = mul(float3(rotated * particle.data.w, 0), (float3x3)view);
    VertexOutput output;
    output.position = mul(viewProjection, float4(particle.centerRotation.xyz + offset, 1));
    output.color = particle.color;
    output.uv = corner + 0.5;
    output.data = particle.data;
    output.centerView = mul(view, float4(particle.centerRotation.xyz, 1)).xyz;
    return output;
}
[shader("vertex")] VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1));
    output.color = input.color;
    output.uv = input.uv;
    output.data = input.data;
    output.centerView = mul(view, float4(input.center, 1)).xyz;
    return output;
}
#define uInverseProjection (inverseProjection)
#define uColor (values[0])
#define uEmission (values[1].xyz)
#define uHasAlbedoTexture (values[1].w)
#define uParticleRenderShape (int(values[2].x))
#define uParticleRenderMode (int(values[2].y))
#define uSoftParticlesEnabled (int(values[2].z))
#define uSoftParticleDistance (values[2].w)
#define uFlipbookColumns (int(values[3].x))
#define uFlipbookRows (int(values[3].y))
#define uFlipbookFramesPerSecond (values[3].z)
#define uFlipbookLooping (int(values[3].w))
#define uFlipbookRandomStart (int(values[4].x))
#define uSmokeLightingEnabled (int(values[4].y))
#define uSmokeLightingStrength (values[4].z)
#define uSmokeAmbient (values[4].w)
#define uSmokeLightDirectionView (values[5].xyz)
#define uSmokeLightColor (values[6].xyz)
#define uVolumeDensity (values[7].x)
#define uVolumeNoiseStrength (values[7].y)
#define uVolumeNoiseFrequency (values[7].z)
#define uVolumeEdgeSoftness (values[7].w)
#define uVolumeSelfShadow (values[8].x)
#define uLocalSmokeLightCount (int(values[8].y))
float hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash21(cell), hash21(cell + float2(1.0, 0.0)), f.x),
                lerp(hash21(cell + float2(0.0, 1.0)), hash21(cell + float2(1.0)), f.x), f.y);
}

float viewDepth(float2 uv, float depth)
{
    if (values[23].z > 0.5)
        uv.y = 1.0 - uv.y;
    float4 viewPosition = mul(uInverseProjection, float4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0));
    return abs(viewPosition.z / max(abs(viewPosition.w), 0.000001));
}

[shader("fragment")] float4 fragmentMain(VertexOutput input) : SV_Target
{
    float mask = 1.0;
    if (uParticleRenderShape == 0 || uParticleRenderMode == 1)
    {
        float2 centered = input.uv * 2.0 - 1.0;
        mask = smoothstep(1.0, 0.82, dot(centered, centered));
    }

    float2 textureUv = input.uv;
    int frameCount = max(uFlipbookColumns * uFlipbookRows, 1);
    if (frameCount > 1)
    {
        float frame = floor(input.data.x * uFlipbookFramesPerSecond);
        if (uFlipbookRandomStart != 0)
        {
            frame += floor(input.data.z * float(frameCount));
        }
        frame = uFlipbookLooping != 0 ? fmod(frame, float(frameCount)) : min(frame, float(frameCount - 1));
        float column = fmod(frame, float(uFlipbookColumns));
        float row = floor(frame / float(uFlipbookColumns));
        textureUv = (input.uv + float2(column, float(uFlipbookRows - 1) - row)) /
                    float2(float(uFlipbookColumns), float(uFlipbookRows));
    }

    float4 materialColor = uColor;
    if (uHasAlbedoTexture > 0.5)
    {
        materialColor *= uAlbedoTexture.Sample(textureUv);
    }

    float4 color = input.color * materialColor;
    color.a *= mask;
    float opticalDepth = 0.0;
    if (uParticleRenderMode == 1)
    {
        float2 volumePosition = input.uv * 2.0 - 1.0;
        float radiusSquared = dot(volumePosition, volumePosition);
        if (radiusSquared >= 1.0)
        {
            discard;
        }
        float chord = 2.0 * sqrt(max(1.0 - radiusSquared, 0.0)) * max(input.data.w, 0.0001);
        float edgeDensity = pow(max(1.0 - radiusSquared, 0.0), uVolumeEdgeSoftness);
        float2 noisePosition =
            volumePosition * uVolumeNoiseFrequency + float2(input.data.z * 17.0, input.data.x * 0.17);
        float noiseValue = valueNoise(noisePosition) * 0.65 + valueNoise(noisePosition * 2.03 + 7.1) * 0.35;
        float densityNoise = lerp(1.0, clamp(noiseValue * 1.6, 0.05, 1.5), uVolumeNoiseStrength);
        opticalDepth = max(uVolumeDensity, 0.0) * edgeDensity * densityNoise * chord;
        color.a = (1.0 - exp(-opticalDepth)) * color.a;
    }
    if (uSoftParticlesEnabled != 0)
    {
        float2 screenUv = input.position.xy / (1.0 / values[23].xy);
        float sceneDistance = viewDepth(screenUv, uSceneDepth.Sample(screenUv).r);
        float particleDistance = viewDepth(screenUv, input.position.z);
        color.a *= clamp((sceneDistance - particleDistance) / max(uSoftParticleDistance, 0.0001), 0.0, 1.0);
    }
    if (color.a <= 0.01)
    {
        discard;
    }

    if (uSmokeLightingEnabled != 0)
    {
        float2 centered = input.uv * 2.0 - 1.0;
        float radial = dot(centered, centered);
        float3 normalView = normalize(float3(centered, sqrt(max(1.0 - radial, 0.02))));
        float diffuse = max(dot(normalView, normalize(uSmokeLightDirectionView)), 0.0);
        if (uParticleRenderMode == 1)
        {
            diffuse = diffuse * exp(-opticalDepth * uVolumeSelfShadow) + 0.2 * (1.0 - exp(-opticalDepth));
        }
        float3 directLight = uSmokeLightColor * diffuse;
        for (int lightIndex = 0; lightIndex < 4; ++lightIndex)
        {
            if (lightIndex >= uLocalSmokeLightCount)
                break;
            float3 toLight = values[9 + lightIndex].xyz - input.centerView;
            float distanceToLight = length(toLight);
            float range = max(values[9 + lightIndex].w, 0.0001);
            if (distanceToLight >= range)
                continue;
            float3 lightDirection = toLight / max(distanceToLight, 0.0001);
            float attenuation = (1.0 - smoothstep(range * 0.9, range, distanceToLight)) / max(distanceToLight * distanceToLight, 0.0001);
            if (values[13 + lightIndex].w == 2.0)
            {
                const float cone = dot(-lightDirection, normalize(values[17 + lightIndex].xyz));
                attenuation *= smoothstep(0.90, 0.975, cone);
            }
            float localDiffuse = max(dot(normalView, lightDirection), 0.0);
            if (uParticleRenderMode == 1)
                localDiffuse =
                    localDiffuse * exp(-opticalDepth * uVolumeSelfShadow) + 0.12 * (1.0 - exp(-opticalDepth));
            directLight += values[13 + lightIndex].xyz * localDiffuse * attenuation;
        }
        // Ambient and direct response are independent controls.
        // A zero lighting strength now removes direct light
        // instead of reverting to fully bright unlit albedo.
        float3 lighting = float3(uSmokeAmbient) + directLight * uSmokeLightingStrength;
        color.rgb *= max(lighting, float3(0.0));
    }

    // Emission represents radiance carried by the participating
    // medium, so empty/transparent portions must not glow.
    float3 emissive = max(uEmission, float3(0.0)) * (values[21].x > 0.5 ? 1.0 : color.a);
    return float4((color.rgb + emissive) * color.a, color.a);
}
