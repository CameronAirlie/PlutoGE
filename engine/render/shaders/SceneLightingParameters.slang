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
    // xyz: spot or rectangle direction; w: local light type (localLightType).
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
    // Clustered local lights (LightClusters.slang): the culling projection, and first-slice depth, slices per
    // log-depth unit, farthest light reach, enabled.
    column_major float4x4 lightClusterProjection;
    float4 lightClusterDepth;
    // Local light emitter shapes. Points and spots (SurfaceBrdf.slang brdfAreaLight): sphere radius, tube axis times
    // length. Rectangles (LtcAreaLights.slang): half-height, right axis times half-width.
    float4 pointShape[32];
    // Contact shadows (BasicLit.slang contactShadowVisibility): xyz the light each mask channel was traced for
    // (-1 none, ContactShadowDirectional, or a local light index); w the mask is present.
    float4 contactShadowParameters;
    // Live sky irradiance for ambient volumes baked with a live sky (SkyIrradiance.h): cosine-convolved L2 SH of
    // mean radiance, in AmbientProbeSampling's coefficient order. [0].w is 1 when a physical sky supplied it.
    float4 skyIrradiance[9];
    // Weather on surfaces (SurfaceWetness.slang): x wetness 0..1; yzw reserved.
    float4 surfaceWeather;
};

// Local light types, as BasicRenderer packs them into pointDirectionSpot.w: points, then spots, then rectangles.
static const uint LocalLightPoint = 0;
static const uint LocalLightSpot = 1;
static const uint LocalLightRect = 2;
uint localLightType(uint light) { return uint(pointDirectionSpot[light].w + 0.5); }
