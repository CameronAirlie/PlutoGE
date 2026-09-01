struct PostProcessParameterBlock
{
    float exposure;
    float gamma;
    uint flipY;
    uint quality;
    float2 inverseResolution;
    float time;
    float padding;
    float4 parameters[6];
    column_major float4x4 inverseViewProjection;
    float4 cameraPosition;
};
