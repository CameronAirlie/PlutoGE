#include "PlutoGE/render/postprocess/DepthOfFieldEffect.h"

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBoolParameter(const std::string &value)
        {
            return value == "1" || value == "true" || value == "True";
        }
    }

    std::vector<PostProcessParameter> DepthOfFieldEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Quality", .type = PostProcessParameterType::Enum, .value = std::to_string(static_cast<int>(m_quality)), .enumOptions = {"Balanced", "High", "Cinematic"}},
            PostProcessParameter{.name = "Auto Focus", .type = PostProcessParameterType::Bool, .value = m_autoFocus ? "true" : "false"},
            PostProcessParameter{.name = "Focus Distance", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusDistance)},
            PostProcessParameter{.name = "Focus Range", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusRange)},
            PostProcessParameter{.name = "Max Blur Radius", .type = PostProcessParameterType::Float, .value = std::to_string(m_maxBlurRadius)},
            PostProcessParameter{.name = "Near Blur Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_nearBlurScale)},
            PostProcessParameter{.name = "Far Blur Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_farBlurScale)},
            PostProcessParameter{.name = "Focus Speed", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusSpeed)},
            PostProcessParameter{.name = "Focus X", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusX)},
            PostProcessParameter{.name = "Focus Y", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusY)},
            PostProcessParameter{.name = "Focus Window", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusWindow)},
        };
    }

    void DepthOfFieldEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Quality")
            {
                m_quality = static_cast<DepthOfFieldQuality>(std::clamp(std::stoi(parameter.value), 0, 2));
            }
            else if (parameter.name == "Auto Focus")
            {
                m_autoFocus = ParseBoolParameter(parameter.value);
                ResetFocus();
            }
            else if (parameter.name == "Focus Distance")
            {
                m_focusDistance = std::clamp(std::stof(parameter.value), 0.01f, 10000.0f);
                if (!m_autoFocus)
                {
                    m_currentFocusDistance = m_focusDistance;
                    m_hasFocusDistance = true;
                }
            }
            else if (parameter.name == "Focus Range")
            {
                m_focusRange = std::clamp(std::stof(parameter.value), 0.01f, 10000.0f);
            }
            else if (parameter.name == "Max Blur Radius")
            {
                m_maxBlurRadius = std::clamp(std::stof(parameter.value), 0.0f, 64.0f);
            }
            else if (parameter.name == "Near Blur Scale")
            {
                m_nearBlurScale = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
            else if (parameter.name == "Far Blur Scale")
            {
                m_farBlurScale = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
            else if (parameter.name == "Focus Speed")
            {
                m_focusSpeed = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Focus X")
            {
                m_focusX = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Focus Y")
            {
                m_focusY = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Focus Window")
            {
                m_focusWindow = std::clamp(std::stof(parameter.value), 0.005f, 0.5f);
            }
        }
    }

    void DepthOfFieldEffect::Initialize()
    {
        ShaderSource source;
        source.vertexSource = R"(
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

        source.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uSceneDepthTexture;
            uniform float uNearPlane;
            uniform float uFarPlane;
            uniform float uFocusDistance;
            uniform float uFocusRange;
            uniform float uMaxBlurRadius;
            uniform float uNearBlurScale;
            uniform float uFarBlurScale;
            uniform int uQuality;

            const float GOLDEN_ANGLE = 2.39996323;

            float LinearizeDepth(float depth)
            {
                depth = 1.0 - depth;
                float z = depth * 2.0 - 1.0;
                return (2.0 * uNearPlane * uFarPlane) / max(uFarPlane + uNearPlane - z * (uFarPlane - uNearPlane), 0.0001);
            }

            float ComputeSignedCoC(float viewDepth)
            {
                float signedDistance = (viewDepth - uFocusDistance) / max(uFocusRange, 0.0001);
                float scale = signedDistance < 0.0 ? uNearBlurScale : uFarBlurScale;
                return clamp(signedDistance * scale, -1.0, 1.0);
            }

            vec3 SampleScene(vec2 uv)
            {
                return max(texture(uSceneTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb, vec3(0.0));
            }

            void main()
            {
                vec2 texelSize = 1.0 / vec2(textureSize(uSceneTexture, 0));
                float centerDepthRaw = texture(uSceneDepthTexture, UV).r;
                if (centerDepthRaw <= 0.000001 || uMaxBlurRadius <= 0.001)
                {
                    FragColor = vec4(SampleScene(UV), 1.0);
                    return;
                }

                float centerDepth = LinearizeDepth(centerDepthRaw);
                float centerCoC = ComputeSignedCoC(centerDepth);
                float centerBlurPixels = abs(centerCoC) * uMaxBlurRadius;
                if (centerBlurPixels < 0.35)
                {
                    FragColor = vec4(SampleScene(UV), 1.0);
                    return;
                }

                int sampleCount = uQuality == 0 ? 18 : (uQuality == 1 ? 34 : 56);
                vec3 accumulatedColor = SampleScene(UV);
                float accumulatedWeight = 1.0;

                for (int sampleIndex = 0; sampleIndex < 56; ++sampleIndex)
                {
                    if (sampleIndex >= sampleCount)
                    {
                        break;
                    }

                    float sampleRatio = (float(sampleIndex) + 0.5) / float(sampleCount);
                    float diskRadius = sqrt(sampleRatio);
                    float angle = float(sampleIndex) * GOLDEN_ANGLE;
                    vec2 disk = vec2(cos(angle), sin(angle)) * diskRadius;
                    float sampleRadiusPixels = diskRadius * uMaxBlurRadius;
                    vec2 sampleUv = UV + disk * uMaxBlurRadius * texelSize;

                    float sampleDepthRaw = texture(uSceneDepthTexture, clamp(sampleUv, vec2(0.0), vec2(1.0))).r;
                    float sampleDepth = sampleDepthRaw <= 0.000001 ? uFarPlane : LinearizeDepth(sampleDepthRaw);
                    float sampleCoC = ComputeSignedCoC(sampleDepth);
                    float sampleBlurPixels = abs(sampleCoC) * uMaxBlurRadius;

                    float centerCoverage = smoothstep(sampleRadiusPixels - 1.0, sampleRadiusPixels + 1.0, centerBlurPixels);
                    float sampleCoverage = smoothstep(sampleRadiusPixels - 1.0, sampleRadiusPixels + 1.0, sampleBlurPixels);

                    bool sampleIsForeground = sampleDepth < centerDepth;
                    float foregroundBleed = (sampleCoC < 0.0 && sampleIsForeground) ? 1.0 : 0.0;
                    float backgroundBlur = (centerCoC > 0.0 && sampleDepth >= centerDepth) ? centerCoverage : 0.0;
                    float nearBlur = (centerCoC < 0.0) ? centerCoverage : 0.0;
                    float sampleWeight = max(max(backgroundBlur, nearBlur), sampleCoverage * foregroundBleed);

                    float edgeGuard = sampleIsForeground && centerCoC > 0.0 ? 0.35 : 1.0;
                    sampleWeight *= edgeGuard;
                    if (sampleWeight <= 0.0001)
                    {
                        continue;
                    }

                    vec3 sampleColor = SampleScene(sampleUv);
                    accumulatedColor += sampleColor * sampleWeight;
                    accumulatedWeight += sampleWeight;
                }

                vec3 color = accumulatedColor / max(accumulatedWeight, 0.0001);
                FragColor = vec4(color, 1.0);
            }
        )";

        m_shader = Shader::Create(source);
    }

    void DepthOfFieldEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        const float targetFocusDistance = m_autoFocus ? ReadAutoFocusDistance(context) : m_focusDistance;
        if (!m_hasFocusDistance)
        {
            m_currentFocusDistance = targetFocusDistance;
            m_hasFocusDistance = true;
        }
        else
        {
            const float focusBlend = m_autoFocus ? m_focusSpeed : 1.0f;
            m_currentFocusDistance += (targetFocusDistance - m_currentFocusDistance) * std::clamp(focusBlend, 0.0f, 1.0f);
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
        m_shader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
        m_shader->SetUniform("uFocusDistance", std::max(m_currentFocusDistance, 0.01f));
        m_shader->SetUniform("uFocusRange", std::max(m_focusRange, 0.01f));
        m_shader->SetUniform("uMaxBlurRadius", std::max(m_maxBlurRadius, 0.0f));
        m_shader->SetUniform("uNearBlurScale", std::max(m_nearBlurScale, 0.0f));
        m_shader->SetUniform("uFarBlurScale", std::max(m_farBlurScale, 0.0f));
        m_shader->SetUniform("uQuality", static_cast<int>(m_quality));
        DrawFullscreenTriangle();

        EndApply();
    }

    float DepthOfFieldEffect::ReadAutoFocusDistance(const PostProcessContext &context)
    {
        auto *source = context.sourceRenderTarget;
        if (!source || source->GetFramebufferID() == 0)
        {
            return m_hasFocusDistance ? m_currentFocusDistance : m_focusDistance;
        }

        const int width = source->GetWidth();
        const int height = source->GetHeight();
        if (width <= 0 || height <= 0)
        {
            return m_hasFocusDistance ? m_currentFocusDistance : m_focusDistance;
        }

        const int sampleExtent = std::clamp(static_cast<int>(std::round(std::min(width, height) * m_focusWindow)), 3, 31);
        const int sampleSize = sampleExtent | 1;
        const int halfSampleSize = sampleSize / 2;
        const int centerX = std::clamp(static_cast<int>(std::round(m_focusX * static_cast<float>(width - 1))), halfSampleSize, width - 1 - halfSampleSize);
        const int centerY = std::clamp(static_cast<int>(std::round(m_focusY * static_cast<float>(height - 1))), halfSampleSize, height - 1 - halfSampleSize);
        const int readX = centerX - halfSampleSize;
        const int readY = centerY - halfSampleSize;

        std::vector<float> depthSamples(static_cast<std::size_t>(sampleSize * sampleSize), 1.0f);
        GLint previousReadFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebufferID());
        glReadPixels(readX, readY, sampleSize, sampleSize, GL_DEPTH_COMPONENT, GL_FLOAT, depthSamples.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);

        const float nearPlane = context.renderContext.cameraData.nearPlane;
        const float farPlane = context.renderContext.cameraData.farPlane;
        float weightedDepth = 0.0f;
        float totalWeight = 0.0f;
        float nearestDepth = farPlane;

        for (int y = 0; y < sampleSize; ++y)
        {
            for (int x = 0; x < sampleSize; ++x)
            {
                const float depth = depthSamples[static_cast<std::size_t>(y * sampleSize + x)];
                if (depth <= 0.000001f)
                {
                    continue;
                }

                const float viewDepth = LinearizeDepth(depth, nearPlane, farPlane);
                nearestDepth = std::min(nearestDepth, viewDepth);
                const float nx = (static_cast<float>(x) - static_cast<float>(halfSampleSize)) / static_cast<float>(halfSampleSize + 1);
                const float ny = (static_cast<float>(y) - static_cast<float>(halfSampleSize)) / static_cast<float>(halfSampleSize + 1);
                const float centerWeight = std::exp(-(nx * nx + ny * ny) * 3.0f);
                const float foregroundWeight = 1.0f / std::max(viewDepth, 0.1f);
                const float weight = centerWeight * (0.35f + foregroundWeight);
                weightedDepth += viewDepth * weight;
                totalWeight += weight;
            }
        }

        if (totalWeight <= 0.0001f)
        {
            return m_hasFocusDistance ? m_currentFocusDistance : m_focusDistance;
        }

        const float averageDepth = weightedDepth / totalWeight;
        return std::clamp(averageDepth * 0.82f + nearestDepth * 0.18f, nearPlane, farPlane);
    }

    float DepthOfFieldEffect::LinearizeDepth(float depth, float nearPlane, float farPlane) const
    {
        depth = 1.0f - depth;
        const float z = depth * 2.0f - 1.0f;
        return (2.0f * nearPlane * farPlane) / std::max(farPlane + nearPlane - z * (farPlane - nearPlane), 0.0001f);
    }

    void DepthOfFieldEffect::ResetFocus()
    {
        m_hasFocusDistance = false;
    }
}
