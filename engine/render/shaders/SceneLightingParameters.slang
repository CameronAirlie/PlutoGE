// Per-frame camera, lighting and ambient-probe parameters shared by the
// forward surface shader and screen-space passes that reuse its uniform
// buffer. The layout must match BasicRenderer's FrameParameters.

[[vk::binding(31, 0)]] Sampler2D diffuseProbeAtlas : register(t31, space0);

#include "../include/PlutoGE/render/AmbientProbeConfig.h"
#include "../include/PlutoGE/render/PhysicalSkyLayout.h"
#include "CloudOcclusion.slang"
struct AmbientProbeVolumeParameters
{
    float4 origin, size, resolutionBase;
    float4 worldToGridRows[3];
};

[[vk::binding(0, 0)]]
cbuffer CameraParameters : register(b0, space0)
{
    column_major float4x4 viewProjection;
    float4 cameraPositionAmbient;
    float4 directionalDirectionIntensity;
    float4 directionalColor;
    column_major float4x4 shadowMatrices[4];
    uint shadowsEnabled;
    uint shadowFlipY;
    float shadowDepthScale;
    float shadowDepthBias;
    float4 shadowInverseResolutions[4];
    float4 shadowCascadeSplits;
    float4 shadowCascadeMetrics[4];
    float4 shadowCascadeParameters;
    float4 shadowFilterParameters;
    float4 shadowFilterEdgeParameters;
    column_major float4x4 view;
    column_major float4x4 motionViewProjection;
    column_major float4x4 previousViewProjection;
    float4 physicalSkyParameters[PLUTO_SKY_LANE_COUNT];
    float4 physicalSkySettings;
    CloudOcclusionParameters cloudOcclusion;
    float4 temporalClipOffset; // clip jitter xy, temporal accumulation, dither frame
    float4 pointPositionRange[32];
    float4 pointColorIntensity[32];
    float4 pointSettings[32];
    float4 pointDirectionSpot[32];
    column_major float4x4 pointShadowMatrices[24];
    float4 pointParameters;
    float4 iblOriginIntensity[4];
    float4 iblSizeBlend[4];
    float4 iblSettings[4];
    float4 iblIrradiance[36];
    float4 probeOrigin, probeSize, probeResolution;
    float4 probeAtlasParameters;
    AmbientProbeVolumeParameters probeVolumes[PLUTO_AMBIENT_PROBE_VOLUMES];
    float4 geometryParameters;
};
