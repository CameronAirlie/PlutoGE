struct PostProcessParameterBlock
{
    float exposure;
    float gamma;
    uint flipY;
    uint quality;
    float2 inverseResolution;
    float time;
    uint zeroToOneDepth;
    float4 parameters[6];
    column_major float4x4 inverseViewProjection;
    column_major float4x4 view;
    column_major float4x4 projection;
    float4 cameraPosition;
    column_major float4x4 worldToLocal;
};
