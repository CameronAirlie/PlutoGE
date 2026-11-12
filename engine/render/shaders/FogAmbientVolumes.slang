// Fog inside baked ambient volumes (AmbientLightVolumeComponent). The fog's sky light (fogEnvironmentLighting) is one
// value for the whole frame, so without this the fog in a cave or a closed room glows with daylight. Volumes baked with
// sky visibility record, per probe, the share of the sky it sees; fog at a point takes the probes' interpolated share
// and is lit by that share of the sky. Fog is a medium, not a surface: the share is the isotropic mean (no normal), and
// the probes are interpolated without the surface visibility tests AmbientProbeSampling.slang makes for receivers.
//
// Requires FogLightingParameters.slang. Reads the probe atlas at binding 31, as BasicLit does. Volumes without sky
// visibility, and atlases without typed descriptors, leave the fog as it was.
#ifndef PLUTO_FOG_AMBIENT_VOLUMES
#define PLUTO_FOG_AMBIENT_VOLUMES
[[vk::binding(31, 0)]] Sampler2D fogProbeAtlas : register(t31, space0);

float4 fogProbeTexel(int index, int row)
{
    const int width = max(int(fogProbeResolution.w), 1);
    return fogProbeAtlas.Load(int3(index % width, (index / width) * PLUTO_AMBIENT_PROBE_ROWS + row, 0));
}

// The share of the sky (0 none, 1 open sky) the fog at `position` sees, composited over overlapping volumes front to
// back like sampleDiffuseGrid; 1 outside every volume with sky visibility.
float fogAmbientSkyVisibility(float3 position)
{
    const int count = int(fogProbeOrigin.w);
    if (count <= 0 || fogProbeAtlasParameters.y < 0.5)
        return 1.0;
    float visibility = 0.0;
    float coverage = 0.0;
    for (int volume = 0; volume < min(count, PLUTO_AMBIENT_PROBE_VOLUMES) && coverage < 0.999; ++volume)
    {
        const FogAmbientProbeVolume descriptor = fogProbeVolumes[volume];
        if (descriptor.worldToGridRows[2].w < 0.5) // baked without sky visibility
            continue;
        const float3 delta = position - descriptor.origin.xyz;
        const float3 x = descriptor.worldToGridRows[0].xyz;
        const float3 local = dot(x, x) > 0.0
            ? float3(dot(x, delta), dot(descriptor.worldToGridRows[1].xyz, delta), dot(descriptor.worldToGridRows[2].xyz, delta))
            : delta;
        const float3 size = max(descriptor.size.xyz, 0.0001);
        const float3 uvw = local / size;
        if (any(uvw < 0.0) || any(uvw > 1.0))
            continue;
        // Fade over the volume's blend distance, as surfaces do.
        const float3 edge = min(uvw, 1.0 - uvw) * size;
        const float blend = descriptor.origin.w;
        const float edgeWeight = blend > 0.0 ? saturate(min(edge.x, min(edge.y, edge.z)) / blend) : 1.0;
        const float weight = max(edgeWeight, 0.0001) * (1.0 - coverage);

        const int3 resolution = max(int3(descriptor.resolutionBase.xyz), int3(1));
        const int base = int(descriptor.resolutionBase.w);
        const float3 grid = uvw * float3(resolution) - 0.5;
        const int3 cell = int3(floor(grid));
        const float3 fraction = frac(grid);
        float share = 0.0;
        float sum = 0.0;
        [ForceUnroll] for (int neighbour = 0; neighbour < 8; ++neighbour)
        {
            const int3 corner = int3(neighbour & 1, (neighbour >> 1) & 1, neighbour >> 2);
            const int3 c = clamp(cell + corner, int3(0), resolution - 1);
            const int index = base + c.x + resolution.x * (c.y + resolution.y * c.z);
            if (fogProbeTexel(index, 0).w < 0.5) // a probe inside geometry
                continue;
            const float3 w = lerp(1.0 - fraction, fraction, float3(corner));
            const float probeWeight = w.x * w.y * w.z;
            // The sky visibility share rides in the .w of SH row 1 (AmbientProbeSampling.slang evaluateProbeLighting).
            share += saturate(fogProbeTexel(index, 2).w) * probeWeight;
            sum += probeWeight;
        }
        if (sum <= 0.0)
            continue;
        visibility += (share / sum) * weight;
        coverage += weight;
    }
    return visibility + (1.0 - coverage);
}
#endif
