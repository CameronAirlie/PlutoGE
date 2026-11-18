// The light visibility mask without a world representation (LightVisibilityCommon.slang): contact shadows only. Every
// backend runs it; slots asking for a world test are left to their contact march.
#include "LightVisibilityCommon.slang"

float worldVisibility(float3 position, float3 normal, float viewDepth, uint slot, float2 noise)
{
    return 1.0;
}

#include "LightVisibilityPass.slang"
