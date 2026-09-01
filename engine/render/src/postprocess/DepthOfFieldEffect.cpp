#include "PlutoGE/render/postprocess/DepthOfFieldEffect.h"
#include "PlutoGE/render/Graphics.h"

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
            PostProcessParameter{.name = "Focal Length (mm)", .type = PostProcessParameterType::Float, .value = std::to_string(m_focalLength)},
            PostProcessParameter{.name = "F-Stop", .type = PostProcessParameterType::Float, .value = std::to_string(m_fStop)},
            PostProcessParameter{.name = "Sensor Width (mm)", .type = PostProcessParameterType::Float, .value = std::to_string(m_sensorWidth)},
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
            else if (parameter.name == "Focal Length (mm)")
            {
                m_focalLength = std::clamp(std::stof(parameter.value), 8.0f, 300.0f);
            }
            else if (parameter.name == "F-Stop")
            {
                m_fStop = std::clamp(std::stof(parameter.value), 0.7f, 32.0f);
            }
            else if (parameter.name == "Sensor Width (mm)")
            {
                m_sensorWidth = std::clamp(std::stof(parameter.value), 4.0f, 70.0f);
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
                const float speed = std::stof(parameter.value);
                // Presets made before physical DoF stored a per-frame blend in
                // [0, 1]. Convert it to an equivalent 60 Hz motor response.
                m_focusSpeed = speed <= 1.0f
                                   ? std::clamp(-std::log(std::max(1.0f - speed, 0.001f)) * 60.0f, 0.1f, 20.0f)
                                   : std::clamp(speed, 0.1f, 20.0f);
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
            uniform float uFocalLength;
            uniform float uFStop;
            uniform float uSensorWidth;
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
                // Thin-lens circle of confusion. Scene distances and focal length
                // are converted to millimetres so the controls match real lenses.
                float subject = max(viewDepth * 1000.0, uFocalLength + 0.01);
                float focus = max(uFocusDistance * 1000.0, uFocalLength + 0.01);
                float cocMm = (uFocalLength * uFocalLength * (subject - focus)) /
                              max(uFStop * subject * (focus - uFocalLength), 0.0001);
                float cocPixels = 0.5 * cocMm * float(textureSize(uSceneTexture, 0).x) /
                                  max(uSensorWidth, 0.001);
                float scale = cocPixels < 0.0 ? uNearBlurScale : uFarBlurScale;
                return clamp(cocPixels * scale / max(uMaxBlurRadius, 0.001), -1.0, 1.0);
            }

            vec3 SampleScene(vec2 uv)
            {
                return max(texture(uSceneTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb, vec3(0.0));
            }

            void main()
            {
                vec2 texelSize = 1.0 / vec2(textureSize(uSceneTexture, 0));
                float centerDepthRaw = texture(uSceneDepthTexture, UV).r;
                if (uMaxBlurRadius <= 0.001)
                {
                    FragColor = vec4(SampleScene(UV), 1.0);
                    return;
                }

                float centerDepth = centerDepthRaw <= 0.000001 ? uFarPlane : LinearizeDepth(centerDepthRaw);
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
            const auto now = std::chrono::steady_clock::now();
            float deltaSeconds = m_lastFocusUpdate.time_since_epoch().count() == 0
                                     ? (1.0f / 60.0f)
                                     : std::chrono::duration<float>(now - m_lastFocusUpdate).count();
            m_lastFocusUpdate = now;
            deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.1f);

            // A damped focus motor is frame-rate independent and deliberately
            // slows down near its target instead of visibly snapping.
            const float focusBlend = m_autoFocus ? 1.0f - std::exp(-m_focusSpeed * deltaSeconds) : 1.0f;
            m_currentFocusDistance += (targetFocusDistance - m_currentFocusDistance) * std::clamp(focusBlend, 0.0f, 1.0f);
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
        m_shader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
        m_shader->SetUniform("uFocusDistance", std::max(m_currentFocusDistance, 0.01f));
        m_shader->SetUniform("uFocalLength", m_focalLength);
        m_shader->SetUniform("uFStop", m_fStop);
        m_shader->SetUniform("uSensorWidth", m_sensorWidth);
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

        if (std::min(width, height) < 3)
        {
            return m_hasFocusDistance ? m_currentFocusDistance : m_focusDistance;
        }

        const int largestOddExtent = std::min(31, (std::min(width, height) - 1) | 1);
        const int sampleExtent = std::clamp(static_cast<int>(std::round(std::min(width, height) * m_focusWindow)), 3, largestOddExtent);
        const int sampleSize = sampleExtent | 1;
        const int halfSampleSize = sampleSize / 2;
        const int centerX = std::clamp(static_cast<int>(std::round(m_focusX * static_cast<float>(width - 1))), halfSampleSize, width - 1 - halfSampleSize);
        const int centerY = std::clamp(static_cast<int>(std::round(m_focusY * static_cast<float>(height - 1))), halfSampleSize, height - 1 - halfSampleSize);
        const int readX = centerX - halfSampleSize;
        const int readY = centerY - halfSampleSize;

        std::vector<float> depthSamples(static_cast<std::size_t>(sampleSize * sampleSize), 1.0f);
        GLint previousReadFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebufferID());
        glReadPixels(readX, readY, sampleSize, sampleSize, GL_DEPTH_COMPONENT, GL_FLOAT, depthSamples.data());
        Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);

        const float nearPlane = context.renderContext.cameraData.nearPlane;
        const float farPlane = context.renderContext.cameraData.farPlane;
        struct WeightedSample { float depth; float weight; };
        std::vector<WeightedSample> validSamples;
        validSamples.reserve(depthSamples.size());

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
                const float nx = (static_cast<float>(x) - static_cast<float>(halfSampleSize)) / static_cast<float>(halfSampleSize + 1);
                const float ny = (static_cast<float>(y) - static_cast<float>(halfSampleSize)) / static_cast<float>(halfSampleSize + 1);
                validSamples.push_back({viewDepth, std::exp(-(nx * nx + ny * ny) * 4.0f)});
            }
        }

        if (validSamples.empty())
        {
            return m_hasFocusDistance ? m_currentFocusDistance : m_focusDistance;
        }

        // A centre-weighted median locks to the dominant subject surface and is
        // not dragged forward by a single weapon/foliage pixel in the AF box.
        std::sort(validSamples.begin(), validSamples.end(), [](const auto &a, const auto &b) { return a.depth < b.depth; });
        float totalWeight = 0.0f;
        for (const auto &sample : validSamples) totalWeight += sample.weight;
        float accumulatedWeight = 0.0f;
        float meteredDepth = validSamples.back().depth;
        for (const auto &sample : validSamples)
        {
            accumulatedWeight += sample.weight;
            if (accumulatedWeight >= totalWeight * 0.5f)
            {
                meteredDepth = sample.depth;
                break;
            }
        }

        const float previous = m_hasFocusDistance ? m_currentFocusDistance : meteredDepth;
        const float deadBand = std::max(0.02f, previous * 0.015f);
        return std::abs(meteredDepth - previous) < deadBand ? previous : std::clamp(meteredDepth, nearPlane, farPlane);
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
        m_lastFocusUpdate = {};
    }

    DepthOfFieldEffect::Settings DepthOfFieldEffect::GetSettings() const noexcept
    {
        return {m_quality, m_focusDistance, m_focalLength, m_fStop, m_sensorWidth,
                m_maxBlurRadius, m_nearBlurScale, m_farBlurScale,
                m_focusX, m_focusY, m_focusWindow, m_autoFocus};
    }
}
