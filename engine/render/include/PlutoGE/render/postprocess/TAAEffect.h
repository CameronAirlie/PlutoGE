#pragma once

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <array>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Shader;

    struct TAAEffectConfig
    {
        float historyWeight = 0.90f;
        float stationaryHistoryWeight = 0.94f;
        float motionHistoryWeight = 0.55f;
        float sharpening = 0.18f;
        float depthRejectionThreshold = 0.0035f;
        float normalRejectionThreshold = 0.82f;
        float velocityRejectionScale = 80.0f;
        float jitterStrength = 1.0f;
        int quality = 0;
        bool jitterEnabled = true;
    };

    class TAAEffect : public ShaderPostProcessEffect
    {
    public:
        explicit TAAEffect(TAAEffectConfig config = {});
        ~TAAEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;

        std::string GetTypeName() const override { return "TAA"; }
        std::string GetDisplayName() const override { return "Temporal Anti-Aliasing"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        CameraData PrepareCameraData(const CameraData &cameraData, int width, int height, std::uint64_t frameSequence);
        void ResetHistory();
        [[nodiscard]] const TAAEffectConfig &GetConfig() const noexcept { return m_config; }

    private:
        static constexpr int kJitterSampleCount = 16;

        void EnsureHistoryTargets(int width, int height);
        glm::vec2 ComputeJitter(std::uint64_t frameSequence, int width, int height) const;
        void BlitResolvedOutput(RenderTarget *source, RenderTarget *destination, RenderTarget *depthSource) const;

        Shader *m_shader = nullptr;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyTargets;
        TAAEffectConfig m_config;
        glm::vec2 m_currentJitter = glm::vec2(0.0f);
        glm::vec2 m_previousJitter = glm::vec2(0.0f);
        int m_width = 0;
        int m_height = 0;
        std::uint8_t m_historyIndex = 0;
        bool m_hasHistory = false;
    };
}
