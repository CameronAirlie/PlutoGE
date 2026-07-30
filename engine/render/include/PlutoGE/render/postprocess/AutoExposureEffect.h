#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    class Shader;

    class AutoExposureEffect : public ShaderPostProcessEffect
    {
    public:
        AutoExposureEffect(
            float keyValue = 0.2f,
            float minExposure = 0.1f,
            float maxExposure = 1.5f,
            float adaptationSpeedUp = 0.18f,
            float adaptationSpeedDown = 0.06f)
            : m_keyValue(keyValue),
              m_minExposure(minExposure),
              m_maxExposure(maxExposure),
              m_adaptationSpeedUp(adaptationSpeedUp),
              m_adaptationSpeedDown(adaptationSpeedDown)
        {
        }

        ~AutoExposureEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "AutoExposure"; }
        std::string GetDisplayName() const override { return "Auto Exposure"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

    private:
        void InitializeExposureResources();
        void ReleaseExposureResources();
        void UpdateExposureTexture(const PostProcessContext &context);

        Shader *m_applyShader = nullptr;
        Shader *m_adaptationShader = nullptr;
        float m_keyValue = 0.18f;
        float m_minExposure = 0.35f;
        float m_maxExposure = 4.0f;
        float m_adaptationSpeedUp = 0.18f;
        float m_adaptationSpeedDown = 0.06f;
        GLuint m_exposureFramebuffer = 0;
        GLuint m_exposureTextures[2] = {0, 0};
        int m_readExposureTextureIndex = 0;
        int m_writeExposureTextureIndex = 1;
        bool m_exposureResourcesInitialized = false;
    };
}
