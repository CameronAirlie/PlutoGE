// Preserve stability for unchanged irradiance, but stop integrating stale
// lighting after a real change. This is local to each pixel/probe; camera
// motion alone does not reset history. An absolute floor protects dark noise.
float vctHistoryWeight(float3 current, float3 history, float baseWeight)
{
    float3 delta = abs(current - history);
    float difference = max(delta.x, max(delta.y, delta.z));
    float3 magnitude = max(abs(current), abs(history));
    float scale = max(0.001, max(magnitude.x, max(magnitude.y, magnitude.z)));
    float changed = smoothstep(0.15, 0.60, difference / scale);
    // Retain accumulation even during continuous motion. A large lighting
    // change can reduce the configured weight by at most 35 percent.
    return saturate(baseWeight) * lerp(1.0, 0.65, changed);
}
