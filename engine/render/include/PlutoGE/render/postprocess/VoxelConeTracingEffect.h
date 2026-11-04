#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <cstddef>

namespace PlutoGE::render
{
    class VoxelConeTracingEffect : public IPostProcessEffect
    {
    public:
        struct Settings
        {
            int resolution = 64;
            int cascadeCount = 2;
            int coneCount = 5;
            int traceResolutionDivisor = 4;
            int updateInterval = 1;
            float updateSpeed = 1.0f;
            int voxelizationCommandBudget = 8;
            int voxelizationLodBias = 0;
            int debugView = 0;
            float volumeSize = 48.0f;
            float intensity = 1.0f;
            float aperture = 0.55f;
            float maxDistance = 24.0f;
            float normalBias = 0.35f;
            float temporalBlend = 0.92f;
            float historyDepthThreshold = 0.25f;
            float historyNormalThreshold = 0.9f;
            bool injectLocalLights = false;
            float localLightBounce = 1.0f;
            float secondaryBounce = 1.0f;
            bool indirectOnly = false;
            bool worldCache = true;
            float cacheSize = 432.0f;
            int cacheUpdates = 64;
        };

        std::string GetTypeName() const override { return "VCTGI"; }
        std::string GetDisplayName() const override { return "Voxel Cone Traced GI"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] Settings GetSettings() const noexcept;

    private:
        static constexpr std::size_t kCascadeCount = 3;

        bool m_worldCache = true;
        float m_cacheSize = 432.0f;
        int m_cacheUpdates = 64;
        int m_requestedCascadeCount = 2;
        int m_resolution = 64;
        int m_coneCount = 5;
        int m_voxelizationLodBias = 0;
        int m_voxelizationCommandBudget = 8;
        int m_updateInterval = 1;
        float m_updateSpeed = 1.0f;
        int m_debugView = 0;
        int m_traceResolutionDivisor = 4;
        float m_volumeSize = 48.0f;
        float m_intensity = 1.0f;
        float m_aperture = 0.55f;
        float m_maxDistance = 24.0f;
        float m_normalBias = 0.35f;
        float m_temporalBlend = 0.92f;
        // World-space view-depth tolerance used for temporal disocclusion.
        float m_historyDepthThreshold = 0.25f;
        float m_historyNormalThreshold = 0.9f;
        bool m_injectLocalLights = false;
        float m_localLightBounce = 1.0f;
        float m_secondaryBounce = 1.0f;
        bool m_indirectOnly = false;
    };
}
