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

    struct SSAOEffectConfig
    {
        std::string typeName = "SSAO";
        std::string displayName = "Screen Space Ambient Occlusion";
        float radius = 0.85f;
        float bias = 0.08f;
        float intensity = 1.25f;
        float power = 1.5f;
        float temporalBlend = 0.9f;
        float historyDepthThreshold = 0.02f;
        float historyNormalThreshold = 0.85f;
        int sampleCount = 16;
        int blurRadius = 1;
        bool halfResolution = true;
    };

    class SSAOEffect : public ShaderPostProcessEffect
    {
    public:
        SSAOEffect();
        explicit SSAOEffect(SSAOEffectConfig config);
        ~SSAOEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        void RenderAmbientOcclusion(const RenderContext &renderContext, RenderTarget *destinationRenderTarget);
        std::string GetTypeName() const override { return m_typeName; }
        std::string GetDisplayName() const override { return m_displayName; }
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
        std::string m_typeName;
        std::string m_displayName;
        glm::mat4 m_previousView = glm::mat4(1.0f);
        glm::mat4 m_previousViewProjection = glm::mat4(1.0f);
        float m_radius = 0.0f;
        float m_bias = 0.0f;
        float m_intensity = 0.0f;
        float m_power = 0.0f;
        float m_temporalBlend = 0.0f;
        float m_historyDepthThreshold = 0.0f;
        float m_historyNormalThreshold = 0.0f;
        int m_sampleCount = 0;
        int m_blurRadius = 0;
        int m_outputMode = 0;
        int m_internalWidth = 0;
        int m_internalHeight = 0;
        GLuint m_noiseTexture = 0;
        std::uint8_t m_historyIndex = 0;
        bool m_hasHistory = false;
        bool m_halfResolution = false;
    };
}