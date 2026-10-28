// Shared AO encodings. Raw AO targets hold visibility and packed edges; history holds visibility, linear view depth
// and the octahedral normal (half-float precision is sufficient for relative history rejection).
#include "ScreenSpaceGuideCommon.slang"

// Four edge-stopping weights (left, right, top, bottom) quantised to two bits each, stored as an exact integer in a
// half-float channel (XeGTAO's packing).
float packEdges(float4 edges)
{
    const float4 quantised = round(saturate(edges) * 3.0);
    return dot(quantised, float4(64.0, 16.0, 4.0, 1.0));
}

float4 unpackEdges(float packed)
{
    const uint value = uint(packed + 0.5);
    return float4((value >> 6) & 3u, (value >> 4) & 3u, (value >> 2) & 3u, value & 3u) / 3.0;
}

// AO is visibility of indirect illumination, not a multiplier on direct light.
// Include additive lighting composites since geometry (SSGI/VCTGI) along with
// geometry's diffuse ambient; the original scene retains direct/emissive light.
float4 ssaoComposite(float4 scene, float3 indirect, float ao, bool diagnostic)
{
    return diagnostic ? float4(ao.xxx,1.0)
        : float4(max(scene.rgb-indirect*(1.0-ao),0.0),scene.a);
}
