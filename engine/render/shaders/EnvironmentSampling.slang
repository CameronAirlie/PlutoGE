#ifndef PLUTO_ENVIRONMENT_SAMPLING
#define PLUTO_ENVIRONMENT_SAMPLING
// Shared cubemap face orientation for captures and the physical sky atlas.
void environmentCubeCoordinate(float3 direction, out int face, out float2 uv)
{
    const float3 a = abs(direction);
    if (a.x >= a.y && a.x >= a.z) {
        face = direction.x >= 0 ? 0 : 1;
        uv = float2(direction.x >= 0 ? -direction.z : direction.z, -direction.y) / max(a.x, 0.000001);
    } else if (a.y >= a.z) {
        face = direction.y >= 0 ? 2 : 3;
        uv = float2(direction.x, direction.y >= 0 ? direction.z : -direction.z) / max(a.y, 0.000001);
    } else {
        face = direction.z >= 0 ? 4 : 5;
        uv = float2(direction.z >= 0 ? direction.x : -direction.x, -direction.y) / max(a.z, 0.000001);
    }
    uv = uv * 0.5 + 0.5;
}

float3 environmentCubeDirection(int face, float2 uv)
{
    uv = uv * 2.0 - 1.0;
    if (face == 0) return float3(1, -uv.y, -uv.x);
    if (face == 1) return float3(-1, -uv.y, uv.x);
    if (face == 2) return float3(uv.x, 1, uv.y);
    if (face == 3) return float3(uv.x, -1, -uv.y);
    if (face == 4) return float3(uv.x, -uv.y, 1);
    return float3(-uv.x, -uv.y, -1);
}
#endif
