#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include <algorithm>

namespace PlutoGE::scene
{
    inline void ApplyDirectionalShadowSettings(render::BasicLighting &lighting, const DirectionalShadowSettings &settings)
    {
        lighting.shadowMethod = settings.method;
        lighting.virtualShadowPageBudget = static_cast<std::uint32_t>(std::clamp(settings.virtualPageBudget, 1, 256));
        lighting.virtualShadowTriangleBudget = static_cast<std::uint32_t>(std::clamp(settings.virtualTriangleBudget, 1, 16000000));
        lighting.shadowResolution = static_cast<std::uint32_t>(std::clamp(
            settings.resolution, 256, 8192));
        lighting.shadowCascadeCount = static_cast<std::uint32_t>(std::clamp(
            settings.cascadeCount, 1, kMaxDirectionalShadowCascades));
        lighting.shadowCascadeResolutionFalloff = std::clamp(
            settings.cascadeResolutionFalloff, 0.25f, 1.0f);
        lighting.shadowNearCascadeDistance = std::max(
            settings.nearCascadeDistance, 0.0f);
        lighting.shadowSplitLambda = std::clamp(
            settings.splitLambda, 0.0f, 1.0f);
        lighting.shadowCascadeBlendDistance = std::max(
            settings.cascadeBlendDistance, 0.0f);
        lighting.shadowSoftness = std::max(settings.softness, 0.0f);
        lighting.shadowFilterEnabled = settings.screenSpaceFilterEnabled;
        lighting.shadowFilterRenderScale = std::clamp(
            settings.screenSpaceFilterRenderScale, 0.25f, 1.0f);
        lighting.shadowFilterRadius = static_cast<std::uint32_t>(std::clamp(
            settings.screenSpaceFilterRadius, 0, 4));
        lighting.shadowFilterDepthScale = std::clamp(
            settings.screenSpaceFilterDepthScale, 0.0f, 0.25f);
        lighting.shadowFilterMinDepthScale = std::clamp(
            settings.screenSpaceFilterMinDepthScale, 0.001f, 2.0f);
        lighting.shadowFilterNormalThreshold = std::clamp(
            settings.screenSpaceFilterNormalThreshold, -1.0f, 1.0f);
        lighting.shadowFilterNormalSoftness = std::max(
            settings.screenSpaceFilterNormalSoftness, 0.001f);
        lighting.shadowDistance = settings.maxDistance;
        lighting.shadowCasterDistance = settings.casterDistance;
    }
}
