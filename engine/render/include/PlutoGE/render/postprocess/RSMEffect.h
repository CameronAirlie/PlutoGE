#pragma once

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Shader;

    enum class RsmDebugOutput
    {
        Indirect = 0,
        CenterFlux,
        OccupiedSamples,
        SampleCoverage,
        SampleCount,
    };

    enum class RsmQualityPreset
    {
        Low = 0,
        Medium,
        High,
    };

    class RSMEffect : public ShaderPostProcessEffect
    {
    public:
        RSMEffect() = default;
        ~RSMEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "RSM"; }
        std::string GetDisplayName() const override { return "Reflective Shadow Maps"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        RenderTarget *GenerateResolvedIndirectLighting(const PostProcessContext &context, int width, int height);

    private:
        void ApplyQualityPreset(RsmQualityPreset preset);
        void ResetHistory();
        void EnsureResources(int captureWidth, int captureHeight, int resolvedWidth, int resolvedHeight);
        void ReleaseCaptureResources();

        Shader *m_captureShader = nullptr;
        Shader *m_resolveShader = nullptr;
        Shader *m_blurShader = nullptr;
        Shader *m_temporalResolveShader = nullptr;
        Shader *m_historyMetadataShader = nullptr;
        std::unique_ptr<RenderTarget> m_rawIndirectRenderTarget;
        std::unique_ptr<RenderTarget> m_blurIntermediateRenderTarget;
        std::unique_ptr<RenderTarget> m_resolvedIndirectRenderTarget;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyColorRenderTargets;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyMetadataRenderTargets;
        unsigned int m_captureFramebuffer = 0;
        unsigned int m_captureInstanceBuffer = 0;
        unsigned int m_normalTexture = 0;
        unsigned int m_fluxTexture = 0;
        unsigned int m_depthTexture = 0;
        float m_intensity = 1.0f;
        float m_captureScale = 1.0f;
        float m_sampleRadius = 256.0f;
        float m_maxDistance = 96.0f;
        float m_normalBias = 0.05f;
        float m_temporalBlend = 0.93f;
        float m_historyDepthThreshold = 0.03f;
        float m_historyNormalThreshold = 0.9f;
        int m_sampleCount = 32;
        int m_captureWidth = 0;
        int m_captureHeight = 0;
        std::size_t m_lastObservedSceneSignature = 0;
        std::size_t m_lastSceneSignature = 0;
        std::size_t m_lastLightSignature = 0;
        glm::mat4 m_previousView = glm::mat4(1.0f);
        glm::mat4 m_previousViewProjection = glm::mat4(1.0f);
        glm::mat4 m_lastObservedViewProjection = glm::mat4(1.0f);
        glm::mat4 m_lastObservedLightSpaceMatrix = glm::mat4(1.0f);
        glm::mat4 m_lastResolvedViewProjection = glm::mat4(1.0f);
        glm::mat4 m_lastResolvedLightSpaceMatrix = glm::mat4(1.0f);
        std::chrono::steady_clock::time_point m_lastResolveTime{};
        std::size_t m_captureInstanceCapacity = 0;
        std::uint8_t m_historyIndex = 0;
        std::uint8_t m_skippedResolveFrames = 0;
        bool m_hasObservedState = false;
        bool m_hasHistory = false;
        RsmQualityPreset m_qualityPreset = RsmQualityPreset::High;
        RsmDebugOutput m_debugOutput = RsmDebugOutput::Indirect;
    };
}