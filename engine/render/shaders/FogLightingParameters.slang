#ifndef PLUTO_FOG_LIGHTING_PARAMETERS
#define PLUTO_FOG_LIGHTING_PARAMETERS
#include "../include/PlutoGE/render/PhysicalSkyLayout.h"
#include "CloudOcclusion.slang"
#include "../include/PlutoGE/render/AmbientProbeConfig.h"

// BasicRenderer's AmbientProbeVolumeParameters (SceneLightingParameters.slang's AmbientProbeVolumeParameters).
struct FogAmbientProbeVolume
{
    float4 origin, size, resolutionBase;
    float4 worldToGridRows[3];
};

// This is the prefix of BasicRenderer's frame block through the fields fog
// consumes (the ambient volumes are the last of them; the local lights and
// reflection captures before them are declared only to keep the layout). Keeping fog on that existing buffer makes directional lighting and
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
    float4 physicalSkyParameters[PLUTO_SKY_LANE_COUNT];
    float4 physicalSkySettings;
    // Includes the cloud shadow map's placement; shaders defining PLUTO_CLOUD_SHADOW_ATLAS sample it.
    CloudOcclusionParameters fogCloudOcclusion;
    float4 fogTemporalClipOffset;
    float4 fogPointPositionRange[32];
    float4 fogPointColorIntensity[32];
    float4 fogPointSettings[32];
    float4 fogPointDirectionSpot[32];
    column_major float4x4 fogPointShadowMatrices[24];
    float4 fogPointParameters;
    float4 fogIblOriginIntensity[4];
    float4 fogIblSizeBlend[4];
    float4 fogIblSettings[4];
    float4 fogIblIrradiance[36];
    // Baked ambient volumes (AmbientProbeSampling.slang's probeOrigin ... probeVolumes): fog reads their sky visibility.
    float4 fogProbeOrigin, fogProbeSize, fogProbeResolution;
    float4 fogProbeAtlasParameters;
    FogAmbientProbeVolume fogProbeVolumes[PLUTO_AMBIENT_PROBE_VOLUMES];
};

#endif
