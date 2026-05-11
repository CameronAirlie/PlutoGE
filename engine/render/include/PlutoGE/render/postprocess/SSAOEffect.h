#pragma once

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <array>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Shader;

    class SSAOEffect : public ShaderPostProcessEffect
    {
    public:
        SSAOEffect() = default;
        ~SSAOEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        void RenderAmbientOcclusion(const RenderContext &renderContext, RenderTarget *destinationRenderTarget);
        std::string GetTypeName() const override { return "SSAO"; }
        std::string GetDisplayName() const override { return "Screen Space Ambient Occlusion"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

    private:
        static constexpr int kKernelSize = 32;

        void EnsureInternalTargets(int width, int height);
        RenderTarget *GenerateResolvedAmbientOcclusion(const RenderContext &renderContext, int width, int height);
        void RenderResolvedAoTexture(const PostProcessContext &context, RenderTarget *resolvedAoTarget, int outputMode);
        void GenerateKernel();
        void GenerateNoiseTexture();
        void ResetHistory();

        Shader *m_ssaoShader = nullptr;
        Shader *m_resolveShader = nullptr;
        Shader *m_compositeShader = nullptr;
        std::unique_ptr<RenderTarget> m_rawAoRenderTarget;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyRenderTargets;
        std::array<glm::vec3, kKernelSize> m_kernel{};
        glm::mat4 m_previousView = glm::mat4(1.0f);
        glm::mat4 m_previousViewProjection = glm::mat4(1.0f);
        float m_radius = 0.85f;
        float m_bias = 0.08f;
        float m_intensity = 1.25f;
        float m_power = 1.5f;
        float m_temporalBlend = 0.9f;
        float m_historyDepthThreshold = 0.02f;
        float m_historyNormalThreshold = 0.85f;
        int m_sampleCount = 16;
        int m_blurRadius = 1;
        int m_outputMode = 0;
        int m_internalWidth = 0;
        int m_internalHeight = 0;
        GLuint m_noiseTexture = 0;
        std::uint8_t m_historyIndex = 0;
        bool m_hasHistory = false;
        bool m_halfResolution = true;
    };
}