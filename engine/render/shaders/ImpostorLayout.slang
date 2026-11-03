// The octahedral impostor layout. Mirrors engine/render/include/PlutoGE/render/ImpostorLayout.h, which documents it;
// keep both in step.
#ifndef PLUTO_IMPOSTOR_LAYOUT
#define PLUTO_IMPOSTOR_LAYOUT

float2 impostorHemiOctahedralEncode(float3 direction)
{
    direction.y = max(direction.y, 0.0);
    const float sum = abs(direction.x) + direction.y + abs(direction.z);
    const float2 octahedral = sum > 0.0 ? float2(direction.x, direction.z) / sum : float2(0.0);
    return float2(octahedral.x + octahedral.y, octahedral.x - octahedral.y) * 0.5 + 0.5;
}

float3 impostorHemiOctahedralDecode(float2 coordinates)
{
    const float2 rotated = coordinates * 2.0 - 1.0;
    const float2 octahedral = float2(0.5 * (rotated.x + rotated.y), 0.5 * (rotated.x - rotated.y));
    return normalize(float3(octahedral.x, 1.0 - abs(octahedral.x) - abs(octahedral.y), octahedral.y));
}

float2 impostorNearestFrame(float3 direction, float framesPerSide)
{
    return clamp(round(impostorHemiOctahedralEncode(direction) * (framesPerSide - 1.0)), 0.0, framesPerSide - 1.0);
}

float3 impostorFrameDirection(float2 frame, float framesPerSide)
{
    return impostorHemiOctahedralDecode(frame / (framesPerSide - 1.0));
}

struct ImpostorFrameBasis
{
    float3 right, up, view;
};

ImpostorFrameBasis impostorBasis(float3 view)
{
    const float3 reference = abs(view.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, -1.0);
    ImpostorFrameBasis basis;
    basis.view = view;
    basis.right = normalize(cross(reference, view));
    basis.up = cross(view, basis.right);
    return basis;
}

#endif
