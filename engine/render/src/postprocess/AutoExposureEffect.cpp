#include "PlutoGE/render/postprocess/AutoExposureEffect.h"

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kSceneTextureSlot = 0;
        constexpr int kPreviousExposureTextureSlot = 1;
        constexpr int kCurrentExposureTextureSlot = 5;
        constexpr float kInitialExposureValue[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    }

    AutoExposureEffect::~AutoExposureEffect()
    {
        ReleaseExposureResources();
    }

    std::vector<PostProcessParameter> AutoExposureEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Key Value",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_keyValue),
            },
            PostProcessParameter{
                .name = "Min Exposure",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_minExposure),
            },
            PostProcessParameter{
                .name = "Max Exposure",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_maxExposure),
            },
            PostProcessParameter{
                .name = "Adapt Up",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_adaptationSpeedUp),
            },
            PostProcessParameter{
                .name = "Adapt Down",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_adaptationSpeedDown),
            },
        };
    }

    void AutoExposureEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Key Value")
            {
                m_keyValue = std::max(std::stof(parameter.value), 0.001f);
            }
            else if (parameter.name == "Min Exposure")
            {
                m_minExposure = std::max(std::stof(parameter.value), 0.001f);
            }
            else if (parameter.name == "Max Exposure")
            {
                m_maxExposure = std::max(std::stof(parameter.value), m_minExposure);
            }
            else if (parameter.name == "Adapt Up")
            {
                m_adaptationSpeedUp = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Adapt Down")
            {
                m_adaptationSpeedDown = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
        }

        m_maxExposure = std::max(m_maxExposure, m_minExposure);
    }

    void AutoExposureEffect::Initialize()
    {
        ReleaseExposureResources();

        ShaderSource applySource;

        applySource.vertexSource = R"(
            #version 330 core

            out vec2 UV;

            void main()
            {
                vec2 vertices[3] = vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5);
            }
        )";

        applySource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uExposureTexture;

            void main()
            {
                vec3 color = texture(uSceneTexture, UV).rgb;
                float exposure = texture(uExposureTexture, vec2(0.5)).r;
                FragColor = vec4(color * max(exposure, 0.0), 1.0);
            }
        )";

        ShaderSource adaptationSource;
        adaptationSource.vertexSource = applySource.vertexSource;
        adaptationSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uPreviousExposureTexture;
            uniform float uKeyValue;
            uniform float uMinExposure;
            uniform float uMaxExposure;
            uniform float uAdaptUp;
            uniform float uAdaptDown;

            float ComputeLuminance(vec3 color)
            {
                return max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.0001);
            }

            void main()
            {
                // Average color from mip 0 (full image average)
                vec3 averageColor = textureLod(uSceneTexture, vec2(0.5), 0.0).rgb;
                float averageLuminance = ComputeLuminance(averageColor);
                float previousExposure = texture(uPreviousExposureTexture, vec2(0.5)).r;
                float targetExposure = clamp(uKeyValue / averageLuminance, min(uMinExposure, uMaxExposure), max(uMinExposure, uMaxExposure));
                float adaptationRate = targetExposure > previousExposure ? uAdaptUp : uAdaptDown;
                float exposure = mix(previousExposure, targetExposure, clamp(adaptationRate, 0.0, 1.0));
                FragColor = vec4(exposure, 0.0, 0.0, 1.0);
            }
        )";

        m_applyShader = Shader::Create(applySource);
        m_adaptationShader = Shader::Create(adaptationSource);
        InitializeExposureResources();
    }

    void AutoExposureEffect::InitializeExposureResources()
    {
        if (m_exposureResourcesInitialized)
        {
            return;
        }

        glGenFramebuffers(1, &m_exposureFramebuffer);
        glGenTextures(2, m_exposureTextures);

        for (GLuint exposureTexture : m_exposureTextures)
        {
            glBindTexture(GL_TEXTURE_2D, exposureTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, kInitialExposureValue);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, m_exposureFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_exposureTextures[m_writeExposureTextureIndex], 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_readExposureTextureIndex = 0;
        m_writeExposureTextureIndex = 1;
        m_exposureResourcesInitialized = true;
    }

    void AutoExposureEffect::ReleaseExposureResources()
    {
        if (m_exposureFramebuffer != 0)
        {
            glDeleteFramebuffers(1, &m_exposureFramebuffer);
            m_exposureFramebuffer = 0;
        }

        if (m_exposureTextures[0] != 0 || m_exposureTextures[1] != 0)
        {
            glDeleteTextures(2, m_exposureTextures);
            m_exposureTextures[0] = 0;
            m_exposureTextures[1] = 0;
        }

        m_readExposureTextureIndex = 0;
        m_writeExposureTextureIndex = 1;
        m_exposureResourcesInitialized = false;
    }

    int AutoExposureEffect::GetSceneTextureMipLevel(const PostProcessContext &context) const
    {
        if (!context.sourceRenderTarget)
        {
            return 0;
        }

        const int maxDimension = std::max(context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight());
        if (maxDimension <= 0)
        {
            return 0;
        }

        return static_cast<int>(std::floor(std::log2(static_cast<float>(maxDimension))));
    }

    void AutoExposureEffect::UpdateExposureTexture(const PostProcessContext &context, int sceneTextureMipLevel)
    {
        if (!m_adaptationShader || !context.sourceRenderTarget || !m_exposureResourcesInitialized)
        {
            return;
        }

        glBindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetColorTextureID());
        glGenerateMipmap(GL_TEXTURE_2D);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glViewport(0, 0, 1, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, m_exposureFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_exposureTextures[m_writeExposureTextureIndex], 0);

        m_adaptationShader->Bind();
        glActiveTexture(GL_TEXTURE0 + kSceneTextureSlot);
        glBindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetColorTextureID());
        m_adaptationShader->SetUniform("uSceneTexture", kSceneTextureSlot);

        glActiveTexture(GL_TEXTURE0 + kPreviousExposureTextureSlot);
        glBindTexture(GL_TEXTURE_2D, m_exposureTextures[m_readExposureTextureIndex]);
        m_adaptationShader->SetUniform("uPreviousExposureTexture", kPreviousExposureTextureSlot);
        // No longer needed: m_adaptationShader->SetUniform("uSceneMipLevel", static_cast<float>(sceneTextureMipLevel));
        m_adaptationShader->SetUniform("uKeyValue", std::max(m_keyValue, 0.001f));
        m_adaptationShader->SetUniform("uMinExposure", std::max(m_minExposure, 0.001f));
        m_adaptationShader->SetUniform("uMaxExposure", std::max(m_maxExposure, m_minExposure));
        m_adaptationShader->SetUniform("uAdaptUp", std::clamp(m_adaptationSpeedUp, 0.0f, 1.0f));
        m_adaptationShader->SetUniform("uAdaptDown", std::clamp(m_adaptationSpeedDown, 0.0f, 1.0f));
        DrawFullscreenTriangle();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void AutoExposureEffect::Apply(const PostProcessContext &context)
    {
        if (!m_applyShader || !m_adaptationShader || !context.sourceRenderTarget)
        {
            return;
        }

        InitializeExposureResources();
        const int sceneTextureMipLevel = GetSceneTextureMipLevel(context);
        UpdateExposureTexture(context, sceneTextureMipLevel);

        BeginApply(context);

        m_applyShader->Bind();
        BindCommonInputs(m_applyShader, context);
        glActiveTexture(GL_TEXTURE0 + kCurrentExposureTextureSlot);
        glBindTexture(GL_TEXTURE_2D, m_exposureTextures[m_writeExposureTextureIndex]);
        m_applyShader->SetUniform("uExposureTexture", kCurrentExposureTextureSlot);
        DrawFullscreenTriangle();

        EndApply();

        std::swap(m_readExposureTextureIndex, m_writeExposureTextureIndex);
    }
}