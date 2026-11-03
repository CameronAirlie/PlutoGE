// Impostor cards (ImpostorLayout.h): one card per instance, in the plane of the atlas frame nearest the view, textured
// with that frame. The cards feed the standard material's fragments unchanged: the renderer binds the atlas as the base
// colour (coverage in alpha) and normal maps (ImpostorDraws.cpp), and each card's tangent frame is its atlas frame's
// basis, so the frame-space normals the baker stored decode as an ordinary tangent-space normal map. Instances arrive
// through their LOD bins; instanceRange.w holds the frames per side and instanceDebugParameters the model-space bounds.
#define PLUTO_STANDARD_MATERIAL 1
#include "BasicLit.slang"
#include "ImpostorLayout.slang"

// Corner ids 0-3 from the card index buffer: x from bit 0, y from bit 1.
float2 impostorCorner(uint vertexId)
{
    return float2((vertexId & 1u) != 0u ? 1.0 : -1.0, (vertexId & 2u) != 0u ? 1.0 : -1.0);
}

VertexOutput impostorCard(uint vertexId, uint instanceId)
{
    const InstanceObjectParameters instance = instanceObject(instanceId);
    const float3 boundsCenter = instanceDebugParameters.xyz;
    const float boundsRadius = instanceDebugParameters.w;
    const float framesPerSide = float(instanceRange.w);
    const float3x3 linear = (float3x3)instance.model;

    // The frame nearest the direction toward the camera, in model space (rotation only; scale is normalized away).
    const float3 worldCenter = mul(instance.model, float4(boundsCenter, 1.0)).xyz;
    const float3 toCamera = normalize(mul(transpose(linear), cameraPositionAmbient.xyz - worldCenter));
    const float2 frame = impostorNearestFrame(toCamera, framesPerSide);
    const ImpostorFrameBasis basis = impostorBasis(impostorFrameDirection(frame, framesPerSide));

    // The card's corner in model space, so the model and its previous transform place it as they place the mesh.
    const float2 corner = impostorCorner(vertexId);
    const float4 modelCorner = float4(boundsCenter + (basis.right * corner.x + basis.up * corner.y) * boundsRadius, 1.0);
    const float4 worldPosition = mul(instance.model, modelCorner);

    VertexOutput output = (VertexOutput)0;
    output.position = mul(viewProjection, worldPosition);
    output.position.xy += temporalClipOffset.xy * output.position.w;
    output.currentClip = mul(motionViewProjection, worldPosition);
    output.previousClip = mul(previousViewProjection, mul(instance.previousModel, modelCorner));
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul(linear, basis.view));
    output.worldTangent = float4(normalize(mul(linear, basis.right)), 1.0);
    // Frame (x, y) occupies atlas cell (x, y); rows run down the image, against the frame's up axis.
    output.uv = (frame + float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5)) / framesPerSide;
    output.lodFade = instanceLodFade(instanceId);
    return output;
}

[shader("vertex")]
VertexOutput impostorVertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_VulkanInstanceID)
{
    return impostorCard(vertexId, instanceId);
}

// Colour-only layouts and coverage depth, as colorInstancedVertexMain.
[shader("vertex")]
VertexOutput impostorColorVertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_VulkanInstanceID)
{
    VertexOutput output = impostorCard(vertexId, instanceId);
    output.previousClip = 0;
    output.currentClip = 0;
    return output;
}
