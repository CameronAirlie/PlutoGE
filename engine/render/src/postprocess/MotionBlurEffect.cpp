#include "PlutoGE/render/postprocess/MotionBlurEffect.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
#include <glad/glad.h>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> MotionBlurEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Quality", .type = PostProcessParameterType::Enum, .value = std::to_string(static_cast<int>(m_quality)), .enumOptions = {"Balanced", "High", "Cinematic"}},
            PostProcessParameter{.name = "Strength", .type = PostProcessParameterType::Float, .value = std::to_string(m_strength)},
            PostProcessParameter{.name = "Shutter Fraction", .type = PostProcessParameterType::Float, .value = std::to_string(m_shutterFraction)},
            PostProcessParameter{.name = "Max Blur Radius", .type = PostProcessParameterType::Float, .value = std::to_string(m_maxBlurRadius)},
            PostProcessParameter{.name = "Velocity Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_velocityThreshold)},
            PostProcessParameter{.name = "Depth Separation Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_depthSeparationScale)},
            PostProcessParameter{.name = "Center Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_centerWeight)},
        };
    }

    void MotionBlurEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Quality")
            {
                m_quality = static_cast<MotionBlurQuality>(std::clamp(std::stoi(parameter.value), 0, 2));
            }
            else if (parameter.name == "Strength")
            {
                m_strength = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Shutter Fraction")
            {
                m_shutterFraction = std::clamp(std::stof(parameter.value), 0.0f, 1.5f);
            }
            else if (parameter.name == "Max Blur Radius")
            {
                m_maxBlurRadius = std::clamp(std::stof(parameter.value), 0.0f, 96.0f);
            }
            else if (parameter.name == "Velocity Threshold")
            {
                m_velocityThreshold = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
            else if (parameter.name == "Depth Separation Scale")
            {
                m_depthSeparationScale = std::clamp(std::stof(parameter.value), 0.0f, 400.0f);
            }
            else if (parameter.name == "Center Weight")
            {
                m_centerWeight = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
        }
    }

    void MotionBlurEffect::Initialize()
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
            uniform sampler2D uSceneMotionTexture;
            uniform float uNearPlane;
            uniform float uFarPlane;
            uniform float uStrength;
            uniform float uShutterFraction;
            uniform float uMaxBlurRadius;
            uniform float uVelocityThreshold;
            uniform float uDepthSeparationScale;
            uniform float uCenterWeight;
            uniform int uQuality;

            float LinearizeDepth(float depth)
            {
                float z = depth * 2.0 - 1.0;
                return (2.0 * uNearPlane * uFarPlane) / max(uFarPlane + uNearPlane - z * (uFarPlane - uNearPlane), 0.0001);
            }

            vec3 SampleScene(vec2 uv)
            {
                return max(texture(uSceneTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb, vec3(0.0));
            }

            vec2 SelectNeighborVelocity(vec2 texelSize, out float centerDepthRaw)
            {
                centerDepthRaw = texture(uSceneDepthTexture, UV).r;
                vec2 bestVelocity = texture(uSceneMotionTexture, UV).xy;
                float bestMagnitude = dot(bestVelocity, bestVelocity);

                for (int y = -1; y <= 1; ++y)
                {
                    for (int x = -1; x <= 1; ++x)
                    {
                        vec2 sampleUv = clamp(UV + vec2(x, y) * texelSize, vec2(0.0), vec2(1.0));
                        vec2 velocity = texture(uSceneMotionTexture, sampleUv).xy;
                        float magnitude = dot(velocity, velocity);
                        if (magnitude > bestMagnitude)
                        {
                            bestMagnitude = magnitude;
                            bestVelocity = velocity;
                        }
                    }
                }

                return bestVelocity;
            }

            void main()
            {
                vec2 textureSizeValue = vec2(textureSize(uSceneTexture, 0));
                vec2 texelSize = 1.0 / textureSizeValue;
                float centerDepthRaw = 1.0;
                vec2 velocityUv = SelectNeighborVelocity(texelSize, centerDepthRaw);
                vec2 velocityPixels = velocityUv * textureSizeValue * uStrength * uShutterFraction;
                float velocityLength = length(velocityPixels);
                vec3 centerColor = SampleScene(UV);

                if (centerDepthRaw >= 0.999999 || velocityLength <= uVelocityThreshold || uMaxBlurRadius <= 0.001)
                {
                    FragColor = vec4(centerColor, 1.0);
                    return;
                }

                velocityPixels *= min(velocityLength, uMaxBlurRadius) / max(velocityLength, 0.0001);
                vec2 blurUv = velocityPixels * texelSize;
                int sampleCount = uQuality == 0 ? 8 : (uQuality == 1 ? 14 : 24);
                float centerDepth = LinearizeDepth(centerDepthRaw);

                vec3 accumulatedColor = centerColor * max(uCenterWeight, 0.0);
                float accumulatedWeight = max(uCenterWeight, 0.0);

                for (int sampleIndex = 0; sampleIndex < 24; ++sampleIndex)
                {
                    if (sampleIndex >= sampleCount)
                    {
                        break;
                    }

                    float t = (float(sampleIndex) + 0.5) / float(sampleCount) - 0.5;
                    vec2 sampleUv = UV + blurUv * t;
                    vec2 clampedUv = clamp(sampleUv, vec2(0.0), vec2(1.0));
                    float sampleDepthRaw = texture(uSceneDepthTexture, clampedUv).r;
                    float sampleDepth = sampleDepthRaw >= 0.999999 ? uFarPlane : LinearizeDepth(sampleDepthRaw);
                    vec2 sampleVelocity = texture(uSceneMotionTexture, clampedUv).xy * textureSizeValue;

                    float depthDelta = abs(sampleDepth - centerDepth) / max(min(sampleDepth, centerDepth), 0.1);
                    float depthWeight = exp(-depthDelta * uDepthSeparationScale);
                    float velocityAgreement = dot(normalize(sampleVelocity + vec2(0.00001)), normalize(velocityPixels + vec2(0.00001)));
                    float velocityWeight = smoothstep(-0.25, 0.5, velocityAgreement);
                    float sampleVelocityLength = length(sampleVelocity);
                    float coverageWeight = smoothstep(abs(t) * velocityLength - 1.0, abs(t) * velocityLength + 1.0, max(sampleVelocityLength, velocityLength));
                    float weight = max(depthWeight * velocityWeight, 0.18 * coverageWeight);
                    weight *= smoothstep(uVelocityThreshold, uVelocityThreshold + 1.0, max(sampleVelocityLength, velocityLength));

                    if (weight <= 0.0001)
                    {
                        continue;
                    }

                    accumulatedColor += SampleScene(clampedUv) * weight;
                    accumulatedWeight += weight;
                }

                FragColor = vec4(accumulatedColor / max(accumulatedWeight, 0.0001), 1.0);
            }
        )";

        m_shader = Shader::Create(source);
    }

    void MotionBlurEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget || !context.renderContext.gBuffer || context.renderContext.gBuffer->GetMotionTextureID() == 0)
        {
            return;
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID());
        m_shader->SetUniform("uSceneMotionTexture", 5);

        m_shader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
        m_shader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
        m_shader->SetUniform("uStrength", m_strength);
        m_shader->SetUniform("uShutterFraction", m_shutterFraction);
        m_shader->SetUniform("uMaxBlurRadius", m_maxBlurRadius);
        m_shader->SetUniform("uVelocityThreshold", m_velocityThreshold);
        m_shader->SetUniform("uDepthSeparationScale", m_depthSeparationScale);
        m_shader->SetUniform("uCenterWeight", m_centerWeight);
        m_shader->SetUniform("uQuality", static_cast<int>(m_quality));
        DrawFullscreenTriangle();

        EndApply();
    }
}
