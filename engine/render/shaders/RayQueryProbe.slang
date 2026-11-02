// Hardware ray tracing check (tests/RayTracingChecks.h): one ray per thread, straight down -z from (x, y, origin z);
// writes the hit distance, the world normal from the hit triangle's positions and the instance's custom index, or
// -1 for a miss. Exercises acceleration structure builds and bindings, ray queries and position fetch.
[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene : register(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> result : register(u1, space0);
[[vk::binding(2, 0)]] StructuredBuffer<float4> origins : register(t2, space0);

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin = origins[id.x].xyz;
    ray.Direction = float3(0.0, 0.0, -1.0);
    ray.TMin = 0.0;
    ray.TMax = 100.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {}
    float4 value = float4(-1.0, 0.0, 0.0, -1.0);
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        const float3 corners[3] = query.CommittedGetIntersectionTriangleVertexPositions();
        const float3 normal = normalize(mul(cross(corners[1] - corners[0], corners[2] - corners[0]),
                                            (float3x3)query.CommittedWorldToObject3x4()));
        // Normal z (sign shows the winding), distance, custom index.
        value = float4(query.CommittedRayT(), normal.z, float(query.CommittedInstanceID()), 1.0);
    }
    result[id.x] = value;
}
