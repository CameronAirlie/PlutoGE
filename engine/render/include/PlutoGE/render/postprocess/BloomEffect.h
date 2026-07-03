#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <glad/glad.h>

#include <memory>
#include <vector>

namespace PlutoGE::render
{
    class RenderTarget;
    class Shader;

    class BloomEffect : public ShaderPostProcessEffect
    {
    public:
        BloomEffect() = default;
        ~BloomEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "Bloom"; }
        std::string GetDisplayName() const override { return "Bloom"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

    private:
        void EnsurePyramid(int width, int height);
        void RenderPrefilter(GLuint sourceTexture, int sourceWidth, int sourceHeight, RenderTarget &destination);
        void RenderDownsample(GLuint sourceTexture, int sourceWidth, int sourceHeight, RenderTarget &destination);
        void RenderCopy(GLuint sourceTexture, RenderTarget &destination);
        void RenderUpsample(GLuint baseTexture, GLuint bloomTexture, int bloomWidth, int bloomHeight, RenderTarget &destination);

        Shader *m_prefilterShader = nullptr;
        Shader *m_downsampleShader = nullptr;
        Shader *m_copyShader = nullptr;
        Shader *m_upsampleShader = nullptr;
        Shader *m_compositeShader = nullptr;

        std::vector<std::unique_ptr<RenderTarget>> m_downsampleTargets;
        std::vector<std::unique_ptr<RenderTarget>> m_upsampleTargets;
        int m_activeMipCount = 0;

        float m_intensity = 0.75f;
        float m_threshold = 1.0f;
        float m_softKnee = 0.5f;
        float m_radius = 0.85f;
        int m_iterations = 6;
    };
}