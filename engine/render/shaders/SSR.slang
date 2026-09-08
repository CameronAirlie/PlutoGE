#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D materialTexture : register(t4, space0);
[[vk::binding(7, 0)]] Sampler2D albedoTexture : register(t7, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

bool projectRay(float3 position, out float2 uv, out float viewDepth)
{
    float4 viewPosition = mul(pass.view, float4(position, 1.0));
    float4 clip = mul(pass.projection, viewPosition);
    viewDepth = -viewPosition.z;
    uv = 0.0;
    if (clip.w <= 0.000001) return false;
    uv = clip.xy / clip.w;
    if (pass.flipY != 0) uv.y = -uv.y;
    uv = uv * 0.5 + 0.5;
    return all(uv >= 0.0) && all(uv <= 1.0);
}

float depthDelta(float2 uv, float rayDepth)
{
    float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return -1e20;
    float3 world = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                            pass.flipY, pass.zeroToOneDepth);
    return rayDepth + mul(pass.view, float4(world, 1.0)).z;
}

float smithG1(float cosine, float alphaSquared)
{
    return 2.0 * cosine / max(cosine + sqrt(alphaSquared +
        (1.0 - alphaSquared) * cosine * cosine), 0.000001);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001 || pass.parameters[0].x <= 0.0) return scene;
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    const float4 material = materialTexture.Sample(uv);
    const float metallic = saturate(material.r);
    const float roughness = clamp(material.g, 0.04, 1.0);
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                                     pass.flipY, pass.zeroToOneDepth);
    const float3 view = normalize(pass.cameraPosition.xyz - position);
    const float ndotv = dot(normal, view);
    if (ndotv <= 0.0) return scene;
    const float3 f0 = lerp(float3(0.04), albedoTexture.Sample(uv).rgb, metallic);
    const float alpha = roughness * roughness;
    const float a2 = alpha * alpha;
    const float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0), normal));
    const float3 bitangent = cross(normal, tangent);
    const float start = max(pass.parameters[0].w, 0.001);
    const float distance = max(pass.parameters[0].y, start);
    const float thickness = max(pass.parameters[0].z, 0.001);
    const float3 origin = position + normal * start;
    const uint steps = clamp(pass.quality, 8u, 128u);
    // Integrate the GGX lobe. Fixed stratified samples avoid temporal shimmer;
    // rejected directions and misses still count in the estimator denominator.
    const uint samples = 16;
    float3 reflection = 0.0;
    for (uint sample = 0; sample < samples; ++sample)
    {
        float xi = (float(sample) + 0.5) / float(samples);
        float phi = 6.283185307 * frac(float(sample) * 0.618033989);
        float cosTheta = sqrt((1.0 - xi) / (1.0 + (a2 - 1.0) * xi));
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        float3 halfVector = tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + normal * cosTheta;
        float vdoth = dot(view, halfVector);
        if (vdoth <= 0.0) continue;
        float3 ray = reflect(-view, halfVector);
        float ndotl = dot(normal, ray);
        if (ndotl <= 0.0) continue;
        float previousTravel = 0.0;
        float previousDelta = -1e20;
        for (uint i = 0; i < steps; ++i)
        {
            float fraction = (float(i) + 1.0) / float(steps);
            float travel = start + distance * fraction * fraction;
            float2 rayUv;
            float rayDepth;
            if (!projectRay(origin + ray * travel, rayUv, rayDepth)) break;
            float delta = depthDelta(rayUv, rayDepth);
            if (previousDelta < 0.0 && delta >= 0.0)
            {
                float low = previousTravel, high = travel;
                for (uint j = 0; j < min(uint(max(pass.parameters[1].w, 0.0)), 8u); ++j)
                {
                    float middle = (low + high) * 0.5;
                    float2 middleUv; float middleDepth;
                    if (!projectRay(origin + ray * middle, middleUv, middleDepth)) break;
                    if (depthDelta(middleUv, middleDepth) >= 0.0) high = middle;
                    else low = middle;
                }
                projectRay(origin + ray * high, rayUv, rayDepth);
                delta = depthDelta(rayUv, rayDepth);
                float3 hitNormal = decodeNormal(normalTexture.Sample(rayUv).xyz);
                if (delta >= 0.0 && delta <= thickness && dot(ray, hitNormal) < -0.01)
                {
                    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(vdoth), 5.0);
                    // BRDF * N.L / pdf, with pdf = D * N.H / (4 * V.H).
                    float weight = smithG1(ndotv, a2) * smithG1(ndotl, a2) * vdoth /
                                   max(ndotv * cosTheta, 0.000001);
                    reflection += sceneTexture.Sample(rayUv).rgb * fresnel * weight *
                                  edgeFade(rayUv, pass.parameters[1].x);
                }
                break;
            }
            previousTravel = travel;
            previousDelta = delta;
        }
    }
    // Indirect specular adds to existing diffuse and direct lighting.
    return float4(scene.rgb + reflection * (pass.parameters[0].x / float(samples)), scene.a);
}
