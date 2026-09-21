struct LocalLight { float4 positionRange; float4 colorIntensity; float4 directionSpot; float4 cone; };
struct VoxelParameters
{
    float3 volumeOrigin;
    float volumeSize;
    uint resolution;
    uint hasDirectionalLight;
    float secondaryBounce;
    uint bounceCascade;
    float4 lightDirectionIntensity;
    float4 lightColor;
    column_major float4x4 shadowMatrices[4];
    column_major float4x4 view;
    float4 shadowCascadeSplits;
    uint shadowsEnabled;
    uint shadowFlipY;
    float shadowDepthScale;
    float shadowDepthBias;
    float4 shadowInverseResolutions[4];
    float4 shadowCascadeParameters;
    uint4 localLightCount;
    LocalLight localLights[16];
};
