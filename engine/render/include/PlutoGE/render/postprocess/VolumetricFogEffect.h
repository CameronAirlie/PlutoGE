#pragma once

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <memory>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Shader;

    class VolumetricFogEffect : public ShaderPostProcessEffect
    {
    public:
        VolumetricFogEffect() = default;
        ~VolumetricFogEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "VolumetricFog"; }
        std::string GetDisplayName() const override { return "Volumetric Fog"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

    private:
        void EnsureInternalTarget(int width, int height);

        Shader *m_shader = nullptr;
        Shader *m_compositeShader = nullptr;
        unsigned int m_shadowCompareSampler = 0;
        std::unique_ptr<RenderTarget> m_fogRenderTarget;
        glm::vec3 m_fogColor{1.0f, 1.0f, 1.0f};
        float m_density = 0.035f;
        float m_heightFalloff = 0.12f;
        float m_heightOffset = 0.0f;
        float m_maxDistance = 80.0f;
        float m_scattering = 0.65f;
        float m_anisotropy = 0.2f;
        float m_ambientContribution = 0.3f;
        float m_directionalContribution = 6.0f;
        float m_maxOpacity = 0.92f;
        int m_stepCount = 16;
        int m_shadowStepStride = 2;
        int m_internalWidth = 0;
        int m_internalHeight = 0;
        bool m_halfResolution = true;
    };
}
