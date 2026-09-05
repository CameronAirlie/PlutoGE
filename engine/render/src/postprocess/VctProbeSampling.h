#pragma once
// GLSL counterpart of shaders/VCTProbeSampling.slang. Keep the lookup equations in sync.
namespace PlutoGE::render { inline constexpr const char* kVctProbeSampling = R"GLSL(
vec3 probeAtlasCoordinate(ivec3 cell, int face)
{
    return (vec3(cell) + vec3(0.5, 0.5, float(face * 16) + 0.5)) / vec3(16, 16, 96);
}
vec4 cachedIrradiance(vec3 position, vec3 normal, vec4 originSize)
{
    vec3 tc = (position - originSize.xyz) / originSize.w;
    if (any(lessThan(tc, vec3(0))) || any(greaterThan(tc, vec3(1)))) return vec4(0.0);
    vec3 grid = tc * 16.0 - 0.5;
    ivec3 base = ivec3(floor(grid)); vec3 fraction = fract(grid);
    vec3 total = vec3(0.0); float sum = 0.0;
    vec3 normalWeight = abs(normal); normalWeight /= max(dot(normalWeight, vec3(1.0)), 0.0001);
    for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
    {
        ivec3 offset = ivec3(x, y, z), cell = clamp(base + offset, ivec3(0), ivec3(15));
        vec3 tri = mix(1.0 - fraction, fraction, vec3(offset));
        vec3 probeToPoint = tc - (vec3(cell) + 0.5) / 16.0;
        float distance = length(probeToPoint);
        vec3 direction = probeToPoint / max(distance, 0.00001);
        vec3 absolute = abs(direction);
        int axis = absolute.x > absolute.y ? (absolute.x > absolute.z ? 0 : 2) : (absolute.y > absolute.z ? 1 : 2);
        int face = axis * 2 + (direction[axis] < 0.0 ? 1 : 0);
        vec2 moments = textureLod(uProbeVisibility, probeAtlasCoordinate(cell, face), 0.0).rg;
        float variance = max(moments.y - moments.x * moments.x, 0.000001);
        float delta = max(distance - moments.x - 0.005, 0.0);
        float visibility = variance / (variance + delta * delta);
        visibility = visibility * visibility * visibility;
        // Suppress probes behind the receiving surface, while allowing smooth interpolation.
        float facing = pow(clamp(dot(-direction, normal) * 0.5 + 0.5, 0.0, 1.0), 2.0);
        vec4 rx = textureLod(uProbeRadiance, probeAtlasCoordinate(cell, normal.x >= 0.0 ? 0 : 1), 0.0);
        vec4 ry = textureLod(uProbeRadiance, probeAtlasCoordinate(cell, normal.y >= 0.0 ? 2 : 3), 0.0);
        vec4 rz = textureLod(uProbeRadiance, probeAtlasCoordinate(cell, normal.z >= 0.0 ? 4 : 5), 0.0);
        vec4 cachedSample = rx * normalWeight.x + ry * normalWeight.y + rz * normalWeight.z;
        float weight = tri.x * tri.y * tri.z * visibility * max(facing, 0.001) * cachedSample.a;
        total += cachedSample.rgb * weight; sum += weight;
    }
    vec3 edge = min(tc, 1.0 - tc);
    float coverage = smoothstep(0.0, 0.1, min(min(edge.x, edge.y), edge.z));
    return vec4(total / max(sum, 0.00001), coverage * smoothstep(0.0, 0.05, sum));
}
)GLSL"; }
