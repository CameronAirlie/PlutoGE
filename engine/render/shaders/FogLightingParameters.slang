#ifndef PLUTO_FOG_LIGHTING_PARAMETERS
#define PLUTO_FOG_LIGHTING_PARAMETERS

// This is the prefix of BasicRenderer's frame block through the fields fog
// consumes. Keeping fog on that existing buffer makes directional lighting and
// cascaded shadows identical to the opaque pass without maintaining a second
// per-frame light representation. Shared by the fog pass and particles.
[[vk::binding(7, 0)]]
cbuffer FogLightingParameters : register(b7, space0)
{
    column_major float4x4 fogViewProjection;
    float4 fogCameraPositionAmbient;
    float4 fogDirectionalDirectionIntensity;
    float4 fogDirectionalColor;
    column_major float4x4 fogShadowMatrices[4];
    uint fogShadowsEnabled;
    uint fogShadowFlipY;
    float fogShadowDepthScale;
    float fogShadowDepthBias;
    float4 fogShadowInverseResolutions[4];
    float4 fogShadowCascadeSplits;
    float4 fogShadowCascadeMetrics[4];
    float4 fogShadowCascadeParameters;
    float4 fogShadowFilterParameters;
    float4 fogShadowFilterEdgeParameters;
    column_major float4x4 fogView;
    column_major float4x4 fogMotionViewProjection;
    column_major float4x4 fogPreviousViewProjection;
    float4 physicalSkyParameters[6];
    float4 physicalSkySettings;
};

#endif
