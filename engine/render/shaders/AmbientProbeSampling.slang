// Baked-volume sampling shared by ordinary, graph and transparent materials.
// The atlas layout is owned by RhiSceneRenderer. Visibility and SH remain exact;
// fixed coefficient/tap counts expose independent texture reads to the compiler.
float4 probeTexel(int index, int row)
{
    const int width = int(probeResolution.w);
    return diffuseProbeAtlas.Load(int3(index % width, (index / width) * PLUTO_AMBIENT_PROBE_ROWS + row, 0));
}

int2 probeVisibilityTexel(int2 texel)
{
    // Octahedral edges join the opposite half of the same edge.
    if (texel.x < 0 || texel.x >= 8) {
        texel.x = texel.x < 0 ? -texel.x-1 : 15-texel.x;
        texel.y = 7-texel.y;
    }
    if (texel.y < 0 || texel.y >= 8) {
        texel.y = texel.y < 0 ? -texel.y-1 : 15-texel.y;
        texel.x = 7-texel.x;
    }
    return texel;
}

float3 probeVisibilityDirection(int2 texel)
{
    texel = probeVisibilityTexel(texel);
    const float2 oct = (float2(texel) + 0.5) / 4.0 - 1.0;
    float3 direction = float3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    if (direction.z < 0)
        direction.xy = (1.0 - abs(oct.yx)) * float2(oct.x >= 0 ? 1 : -1, oct.y >= 0 ? 1 : -1);
    return normalize(direction);
}

float4 preparedProbeHitPoint(int index, int2 texel, float4 surface)
{
    if (probeAtlasParameters.x > 0.0)
    {
        return diffuseProbeAtlas.Load(int3(index % int(probeResolution.w),
            int(probeAtlasParameters.x) + (index / int(probeResolution.w)) * PLUTO_AMBIENT_PROBE_VISIBILITY_ROWS + texel.x + texel.y * 8, 0));
    }
    const float3 point = probeVisibilityDirection(texel) * surface.x;
    return float4(point, dot(surface.yzw, point));
}

float3 probeVisibilityPoint(int index, int2 texel)
{
    texel = probeVisibilityTexel(texel);
    const float4 surface = probeTexel(index, 10 + texel.x + texel.y * 8);
    return preparedProbeHitPoint(index, texel, surface).xyz;
}

// Prove whole-cell visibility/occlusion from conservative baked bounds. Unknown
// returns -1 and preserves all four finite-plane tests, including wall edges.
float summarizedProbeVisibility(int index, int2 cell,
                                   float3 direction, float distance, float bias)
{
    if (probeAtlasParameters.z <= 0.0) return -1.0;
    const int row = int(probeAtlasParameters.z) + (index / int(probeResolution.w)) * PLUTO_AMBIENT_PROBE_SUMMARY_ROWS + ((cell.y+1)*9 + cell.x+1)*2;
    const float4 bounds = diffuseProbeAtlas.Load(int3(index % int(probeResolution.w), row+1, 0));
    const int mode = int(bounds.z);
    const float error = 0.00001 * max(1.0, max(distance, bounds.w));
    if (bounds.y < 0.0) return -1.0;
    // Bound the closest point in the finite hit footprint. Very short rays
    // use the bias-sized footprint rather than half their sampled distance.
    const float minimumPlaneDepth = max(0.0, bounds.y - max(bounds.y * 0.5, bias));
    if (distance + error <= minimumPlaneDepth + bias) return 1.0;
    if (mode == 0)
    {
        if (distance + error <= bounds.y + bias) return 1.0;
        if (distance - error >= bounds.w + bias * 2.0) return 0.0;
    }
    if (mode != 1) return -1.0;
    const float4 plane = diffuseProbeAtlas.Load(int3(index % int(probeResolution.w), row, 0));
    const float denominator = dot(plane.xyz, direction);
    if (abs(denominator) <= 0.00001001) return -1.0;
    const float2 depthRange = float2(plane.w, bounds.x) / denominator;
    const float lowerDepth = min(depthRange.x, depthRange.y), upperDepth = max(depthRange.x, depthRange.y);
    const float projectedDistance = denominator * distance;
    const float planeError = error * max(1.0, abs(denominator));
    const bool onAllPlanes = max(abs(projectedDistance-plane.w), abs(projectedDistance-bounds.x)) + planeError <= bias*2.0;
    if (lowerDepth + bias >= distance + error && (bounds.y + bias >= distance + error || onAllPlanes)) return 1.0;
    const bool offAllPlanes = projectedDistance < plane.w - bias*2.0 - planeError ||
                             projectedDistance > bounds.x + bias*2.0 + planeError;
    if (upperDepth + bias*2.0 <= distance - error && bounds.w + bias*2.0 <= distance - error && offAllPlanes) return 0.0;
    return -1.0;
}

float probeVisibility(int index, float3 direction, float receiverDistance, float bias, bool hasNormals)
{
    float2 oct = direction.xy / max(abs(direction.x) + abs(direction.y) + abs(direction.z), 0.0001);
    if (direction.z < 0)
        oct = (1 - abs(oct.yx)) * float2(oct.x >= 0 ? 1 : -1, oct.y >= 0 ? 1 : -1);
    const float2 coordinate = (oct * 0.5 + 0.5) * 8 - 0.5;
    if (hasNormals) {
        const int2 cell = int2(floor(coordinate));
        const float2 fraction = frac(coordinate);
        const float summary = summarizedProbeVisibility(index, cell, direction, receiverDistance, bias);
        if (summary >= 0.0) return summary;
        float visibility = 0;
        [ForceUnroll] for (int y = 0; y < 2; ++y) [ForceUnroll] for (int x = 0; x < 2; ++x) {
            const int2 texel = probeVisibilityTexel(cell + int2(x,y));
            const float4 surface = probeTexel(index, 10 + texel.x + texel.y * 8);
            const float4 hit = preparedProbeHitPoint(index, texel, surface);
            const float3 hitPoint = hit.xyz;
            const float denominator = dot(surface.yzw, direction);
            float depth = surface.x;
            if (dot(surface.yzw, surface.yzw) > 0.5 && abs(denominator) > 0.00001) {
                const float planeDepth = hit.w / denominator;
                // A visibility texel describes a finite angular footprint, not an
                // infinite plane. At silhouettes (stair risers, wall ends), distant
                // plane extensions must not occlude an unrelated narrow passage.
                const float footprint = max(surface.x * 0.5, bias);
                const float3 separation = direction * planeDepth - hitPoint;
                if (planeDepth > 0 && dot(separation, separation) <= footprint * footprint)
                    depth = planeDepth;
                else {
                    // If the receiver lies on this plane it is unoccluded even
                    // at grazing angles, where projection spans multiple texels.
                    const float planeSeparation = denominator * receiverDistance - hit.w;
                    if (abs(planeSeparation) <= bias * 2) depth = receiverDistance + bias;
                }
            }
            const float2 w = lerp(1-fraction, fraction, float2(x,y));
            const float visible = 1-smoothstep(depth + bias, depth + bias*2, receiverDistance);
            visibility += visible * w.x * w.y;
        }
        return visibility;
    }
    // Keep a full triangle at the map edges. Extrapolating its plane also
    // preserves planar receivers across the octahedral map's seams.
    const int2 cell = clamp(int2(floor(coordinate)), int2(0), int2(6));
    const float2 fraction = coordinate - float2(cell);
    const float3 a = probeVisibilityPoint(index, cell);
    const float3 b = probeVisibilityPoint(index, cell + int2(1, 0));
    const float3 c = probeVisibilityPoint(index, cell + int2(0, 1));
    const float3 d = probeVisibilityPoint(index, cell + int2(1, 1));
    const float3 planeNormal = fraction.x + fraction.y <= 1 ? cross(b-a, c-a) : cross(c-d, b-d);
    const float3 planePoint = fraction.x + fraction.y <= 1 ? a : d;
    const float denominator = dot(planeNormal, direction);
    const float distance = abs(denominator) > 1e-8 ? dot(planeNormal, planePoint) / denominator : -1;
    // Degenerate/discontinuous samples must not produce infinite or negative depths.
    const float visibleDistance = distance > 0 ? distance : min(min(length(a), length(b)), min(length(c), length(d)));
    return receiverDistance <= visibleDistance + bias ? 1 : 0;
}

// Explicit fixed addresses avoid dynamically indexing a per-fragment SH array.
// Retain the reference's accumulation order and all nine directional terms.
float3 evaluateProbeIrradiance(int index, float3 normal)
{
    float3 light = probeTexel(index, 1).rgb * 0.282095;
    light += probeTexel(index, 2).rgb * (0.488603 * normal.y);
    light += probeTexel(index, 3).rgb * (0.488603 * normal.z);
    light += probeTexel(index, 4).rgb * (0.488603 * normal.x);
    light += probeTexel(index, 5).rgb * (1.092548 * normal.x * normal.y);
    light += probeTexel(index, 6).rgb * (1.092548 * normal.y * normal.z);
    light += probeTexel(index, 7).rgb * (0.315392 * (3 * normal.z * normal.z - 1));
    light += probeTexel(index, 8).rgb * (1.092548 * normal.x * normal.z);
    light += probeTexel(index, 9).rgb * (0.546274 * (normal.x * normal.x - normal.y * normal.y));
    return light;
}

// Eight cell neighbours, visibility rejection, normal weighting and normalized
// interpolation. Invalid/occluded probes never contribute light across walls.
float4 sampleDiffuseVolume(float3 position, float3 normal, float3 receiverNormal, float3 uvw, float3 size, float3 res, int base, bool preparedNormals)
{
    if (any(uvw < 0) || any(uvw > 1)) return float4(0);
    const int3 resolution = int3(res);
    const float3 grid = uvw * res - 0.5;
    const int3 cell = int3(floor(grid));
    const float3 fraction = frac(grid);
    float3 irradiance = float3(0);
    float sum = 0;
    const float bias = min(0.002, min(size.x/res.x, min(size.y/res.y, size.z/res.z)) * 0.01);
    [ForceUnroll] for (int neighbour = 0; neighbour < 8; ++neighbour) {
        const int x = neighbour & 1, y = (neighbour >> 1) & 1, z = neighbour >> 2;
        const int3 c = clamp(cell + int3(x,y,z), int3(0), resolution - 1);
        const int index = base + c.x + resolution.x * (c.y + resolution.y * c.z);
        const float4 probe = probeTexel(index, 0);
        if (probe.w < 0.5) continue;
        // A probe on the back side of the receiver cannot illuminate its
        // front face. In particular, a visibility hit on the wall itself is
        // not permission to transfer lighting through that wall.
        if (dot(receiverNormal, probe.xyz-position) < -bias) continue;
        const float3 delta = position + receiverNormal * bias - probe.xyz;
        const float distance = length(delta);
        const float3 direction = delta / max(distance, 0.0001);
        const bool hasNormals = probeAtlasParameters.y > 0.5 ? preparedNormals : probeTexel(index, 1).w > 0.5;
        const float visibility = probeVisibility(index, direction, distance, bias, hasNormals);
        if (visibility <= 0) continue;
        const float3 w = lerp(1-fraction, fraction, float3(x,y,z));
        const float weight = w.x*w.y*w.z * max(0.05, dot(receiverNormal, -direction)) * visibility;
        const float3 light = evaluateProbeIrradiance(index, normal);
        irradiance += max(light,0.0) * weight;
        sum += weight;
    }
    // Within the baked volume, missing visibility means darkness, not fallback
    // sky lighting, which would otherwise reintroduce leaks through solid walls.
    return float4(irradiance / max(sum,0.00001), 1);
}

float4 volumeOrigin(int index) { return probeAtlasParameters.y > 0.5 ? probeVolumes[index].origin : probeTexel(index, 0); }
float4 volumeSize(int index) { return probeAtlasParameters.y > 0.5 ? probeVolumes[index].size : probeTexel(index, 1); }
float4 volumeResolution(int index) { return probeAtlasParameters.y > 0.5 ? probeVolumes[index].resolutionBase : probeTexel(index, 2); }

float3 diffuseVolumeCoordinates(int index, float3 position)
{
    const float3 delta = position - volumeOrigin(index).xyz;
    const float3 x = probeAtlasParameters.y > 0.5 ? probeVolumes[index].worldToGridRows[0].xyz : probeTexel(index,3).xyz;
    const float3 y = probeAtlasParameters.y > 0.5 ? probeVolumes[index].worldToGridRows[1].xyz : probeTexel(index,4).xyz;
    const float3 z = probeAtlasParameters.y > 0.5 ? probeVolumes[index].worldToGridRows[2].xyz : probeTexel(index,5).xyz;
    const float3 local = dot(x,x)>0 ? float3(dot(x,delta),dot(y,delta),dot(z,delta)) : delta;
    return local / volumeSize(index).xyz;
}

float4 sampleDiffuseGrid(float3 position, float3 normal, float3 receiverNormal)
{
    int selected = -1;
    float finest = 1e30;
    for (int i=0; i<int(probeOrigin.w); ++i) {
        const float3 size = volumeSize(i).xyz;
        const float3 uvw = diffuseVolumeCoordinates(i,position);
        if (any(uvw<0) || any(uvw>1)) continue;
        const float3 cellSize = size / volumeResolution(i).xyz;
        const float spacing = max(cellSize.x,max(cellSize.y,cellSize.z));
        if (spacing<finest) { finest=spacing; selected=i; }
    }
    if (selected<0) return float4(0);
    const float4 resolution = volumeResolution(selected);
    return sampleDiffuseVolume(position,normal,receiverNormal,diffuseVolumeCoordinates(selected,position),
        volumeSize(selected).xyz,resolution.xyz,int(resolution.w), volumeSize(selected).w > 0.5);
}
