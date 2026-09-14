#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "OceanLighting.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(15, 0)]] cbuffer Ocean : register(b15, space0)
{
    column_major float4x4 localToWorld;
    float4 shallowOpacity, deepSmoothness, foamVisibility, waves;
    float4 underwater, surface, sunDirectionTime, sunColorIntensity;
    int4 mask;
    int4 areaCounts[8];
    float4 points[256];
    float4 waveShape[8], waveMotion[8];
    float4 crestFoam, detail, flow;
};
#include "OceanWaves.slang"
[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

bool waterAt(float2 p)
{
    if (mask.y == 0) return true;
    bool covered = false;
    for (int area = 0; area < min(mask.y, 8); ++area)
    {
        int count = min(areaCounts[area].x, 32);
        if (count < 3) continue;
        bool inside = false;
        int previous = count - 1;
        for (int current = 0; current < count; ++current)
        {
            float2 a = points[area * 32 + current].xy, b = points[area * 32 + previous].xy;
            if ((a.y > p.y) != (b.y > p.y))
                if (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
            previous = current;
        }
        covered = covered || inside;
    }
    // Default polygons exclude water; inversion limits water to their union.
    return mask.x != 0 ? covered : !covered;
}
// Both views measure only the distance travelled through water, in world units.
// Visibility depth is a hard upper bound even when turbidity or opacity is zero.
float3 waterTransmission(float path)
{
    float ratio = max(path,0.0) / max(foamVisibility.w,.01);
    float fadeStart = clamp(underwater.x,0.0,.99);
    float fadeEnd = min(1.0,fadeStart+max(underwater.y,.01));
    float cutoff = 1.0-smoothstep(fadeStart,fadeEnd,ratio);
    return exp(-float3(2.2,1.4,.8)*ratio*max(surface.w,0.0)*max(underwater.z,0.0))*cutoff;
}
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    float4 original = sceneTexture.Sample(uv);
    float depth = depthTexture.Sample(uv).r;
    float3 origin = mul(pass.worldToLocal, float4(pass.cameraPosition.xyz, 1)).xyz;
    float3 worldRay = reconstructWorldRay(uv, pass.view, pass.projection, pass.flipY);
    float3 ray = mul(pass.worldToLocal, float4(worldRay, 0)).xyz;
    float3 scenePosition = reconstructWorldPosition(uv, max(depth, 0.0000001), pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
    float sceneDistance = depth <= 0.0000001 ? 100000.0 : max(dot(scenePosition - pass.cameraPosition.xyz, worldRay), 0.0);
    float visibility = max(foamVisibility.w, .01);
    float cameraHeight = sampleWaves(origin.xz).height;
    if (origin.y < cameraHeight && waterAt(origin.xz))
    {
        float path = sceneDistance;
        if (ray.y > .000001) path = min(path, (cameraHeight-origin.y) / ray.y);
        float ratio = path / visibility;
        float available = exp(-max(cameraHeight-origin.y, 0.0) / visibility * max(underwater.w, 0.0));
        float3 transmission = waterTransmission(path);
        float3 surfaceWorld = mul(localToWorld, float4(origin.x, cameraHeight, origin.z, 1)).xyz;
        float3 up = normalize(mul(float3(0,1,0), (float3x3)pass.worldToLocal));
        float3 illumination = oceanAmbient(up) + max(directionalColor.rgb, 0.0) *
            max(directionalDirectionIntensity.w, 0.0) * saturate(dot(up, normalize(-directionalDirectionIntensity.xyz))) *
            oceanVisibility(surfaceWorld, up) / PI;
        float3 tint = lerp(shallowOpacity.xyz, deepSmoothness.xyz, saturate(max(ratio, 1.0 - available))) * available * illumination;
        return float4(original.rgb * transmission + tint * (1.0-transmission), original.a);
    }
    if (ray.y >= -0.0000001) return original;
    // Bracket the height field inside its conservative wave envelope. Safeguarded Newton
    // steps avoid the old flat-plane division instability on steep waves and near the horizon.
    float bound = max(crestFoam.w, .0001);
    float lower = max(0.0, (bound-origin.y)/ray.y);
    float upper = min(sceneDistance, (-bound-origin.y)/ray.y);
    if (upper <= lower) return original;
    float distance = clamp(-origin.y/ray.y, lower, upper);
    OceanWaveSample wave;
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        float3 hit = origin + ray * distance;
        wave = sampleWaves(hit.xz);
        float residual = hit.y-wave.height;
        if (abs(residual) < .0005) break;
        if (residual > 0) lower = distance; else upper = distance;
        float derivative = ray.y-dot(wave.gradient,ray.xz);
        float candidate = distance-residual/(abs(derivative)>.00001 ? derivative : -.00001);
        distance = candidate > lower && candidate < upper ? candidate : (lower+upper)*.5;
    }
    if (distance <= 0 || distance >= sceneDistance) return original;
    float3 localHit = origin + ray * distance;
    wave = sampleWaves(localHit.xz);
    if (abs(localHit.y-wave.height) > .02 || !waterAt(localHit.xz)) return original;
    float dx = wave.gradient.x, dz = wave.gradient.y;
    // Small wind ripples enrich close reflections and fade before becoming subpixel noise.
    float rippleFade = (1.0-smoothstep(20.0, 100.0, distance)) * saturate(abs(waves.x)*5.0);
    float2 ripplePhase = float2(dot(localHit.xz,float2(2.8,1.7)), dot(localHit.xz,float2(-1.9,3.4)))
        + sunDirectionTime.w * waves.z * float2(-2.1,2.7);
    float2 rippleSlope = (float2(2.8,1.7)*cos(ripplePhase.x) + float2(-1.9,3.4)*cos(ripplePhase.y)) * detail.x * rippleFade;
    dx += rippleSlope.x;
    dz += rippleSlope.y;
    float3 normal = normalize(mul(float3(-dx,1,-dz), (float3x3)pass.worldToLocal));
    float thickness = max(sceneDistance - distance, 0.0);
    float factor = saturate(thickness / visibility);
    float3 worldHit = pass.cameraPosition.xyz + worldRay * distance;
    float shadow = oceanVisibility(worldHit, normal);
    float3 lightDirection = normalize(-directionalDirectionIntensity.xyz);
    float3 sunlight = max(directionalColor.rgb, 0.0) * max(directionalDirectionIntensity.w, 0.0) * shadow;
    float3 ambient = oceanAmbient(normal);
    float3 irradiance = ambient + sunlight * saturate(dot(normal, lightDirection)) / PI;
    float3 tint = lerp(shallowOpacity.xyz, deepSmoothness.xyz, factor) * irradiance;
    // Forward scattering makes illuminated wave crests glow without emitting light at night.
    float crest = saturate(localHit.y / max(abs(waves.x), .001)) * (1.0-factor*.5);
    tint += shallowOpacity.rgb * sunlight * crest * pow(saturate(dot(worldRay, lightDirection)), 4.0) * .35;
    float2 refractedUv = saturate(uv + mul((float3x3)pass.view, normal).xy * surface.x * saturate(thickness));
    float refractedDepth = depthTexture.Sample(refractedUv).r;
    float3 refractedPosition = reconstructWorldPosition(refractedUv, max(refractedDepth, .0000001), pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
    // Do not pull foreground geometry across the water boundary.
    if (refractedDepth > .0000001 && dot(refractedPosition - pass.cameraPosition.xyz, worldRay) < distance)
    {
        refractedUv = uv;
        refractedDepth = depth;
        refractedPosition = scenePosition;
    }
    float refractedPath = refractedDepth > .0000001 ? length(refractedPosition-worldHit) : 100000.0;
    float3 transmission = waterTransmission(refractedPath);
    float3 bottomLocal = mul(pass.worldToLocal, float4(scenePosition,1)).xyz;
    float caustics = depth > .0000001 && bottomLocal.y < wave.height
        ? waterCaustics(bottomLocal, sunDirectionTime.w) : 0.0;
    float3 bottomLighting = sceneTexture.Sample(refractedUv).rgb * (1.0 + sunlight * caustics);
    float3 refracted = bottomLighting * transmission + tint * (1.0-transmission);
    float fresnel = .0204 + .9796 * pow(1.0 - saturate(dot(-worldRay,normal)), 5.0);
    float3 reflectedDirection = reflect(worldRay, normal);
    // Screen-space normal variance broadens distant highlights to suppress sparkle aliasing.
    float variance = dot(ddx(normal), ddx(normal)) + dot(ddy(normal), ddy(normal));
    float roughness = clamp(sqrt(pow(1.0-saturate(deepSmoothness.w), 2.0) + variance), .06, 1.0);
    float3 reflection = oceanEnvironment(reflectedDirection, roughness, ambient);
    float3 specular = sunlight * oceanSunSpecular(normal, -worldRay, lightDirection, roughness);
    float3 color = refracted * (1.0-fresnel) + reflection * fresnel + specular;
    float shoreDepth = depth > .0000001 ? max(wave.height-bottomLocal.y, 0.0) : foamVisibility.w*100.0;
    float shore = (1.0-smoothstep(0.0,max(surface.y,.001),shoreDepth)) * max(surface.z,0.0);
    float pattern = waterFoamPattern(localHit.xz, sunDirectionTime.w);
    float wash = .65 + .35*sin(shoreDepth*6.0 - sunDirectionTime.w*waves.z*2.0 + pattern*3.0);
    float whitecaps = smoothstep(crestFoam.x,crestFoam.x+.15,wave.crest) * crestFoam.y;
    float foam = saturate(shore*wash*(.35+.65*pattern) + whitecaps*pattern);
    color = lerp(color,foamVisibility.xyz * irradiance,saturate(foam));
    return float4(lerp(refracted,color,saturate(shallowOpacity.w)),original.a);
}
