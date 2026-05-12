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

    class SSGIEffect : public ShaderPostProcessEffect
    {
    public:
        SSGIEffect() = default;
        ~SSGIEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "SSGI"; }
        std::string GetDisplayName() const override { return "Screen Space Global Illumination"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        RenderTarget *GenerateResolvedIndirectLighting(const PostProcessContext &context, int width, int height);
        bool OutputsIndirectOnly() const { return m_outputMode != 0; }

    private:
        static constexpr int kMaxBlurRadius = 4;

        void RenderComposite(const PostProcessContext &context, RenderTarget *resolvedIndirectTarget);
        void EnsureInternalTargets(int width, int height);
        void GenerateNoiseTexture();
        void ResetHistory();

        Shader *m_traceShader = nullptr;
        Shader *m_resolveShader = nullptr;
        Shader *m_historyMetadataShader = nullptr;
        Shader *m_compositeShader = nullptr;
        std::unique_ptr<RenderTarget> m_rawIndirectRenderTarget;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyColorRenderTargets;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyMetadataRenderTargets;
        glm::mat4 m_previousView = glm::mat4(1.0f);
        glm::mat4 m_previousViewProjection = glm::mat4(1.0f);
        float m_intensity = 1.35f;
        float m_rayDistance = 7.5f;
        float m_stepSize = 0.08f;
        float m_thickness = 0.45f;
        float m_depthPhi = 20.0f;
        float m_normalPhi = 12.0f;
        float m_temporalBlend = 0.88f;
        float m_historyDepthThreshold = 0.03f;
        float m_historyNormalThreshold = 0.9f;
        int m_sampleCount = 6;
        int m_stepCount = 10;
        int m_blurRadius = 2;
        int m_outputMode = 0;
        int m_internalWidth = 0;
        int m_internalHeight = 0;
        GLuint m_noiseTexture = 0;
        std::uint8_t m_historyIndex = 0;
        bool m_hasHistory = false;
        bool m_halfResolution = true;
    };
}