#include "PlutoGE/render/postprocess/BloomEffect.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
#include <glad/glad.h>
#include <string>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kSourceTextureSlot = 0;
        constexpr int kBaseTextureSlot = 0;
        constexpr int kBloomTextureSlot = 1;
        constexpr int kCompositeBloomTextureSlot = 5;

        float ReadFloatParameter(const PostProcessParameter &parameter, float fallback)
        {
            try
            {
                return std::stof(parameter.value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        int ReadIntParameter(const PostProcessParameter &parameter, int fallback)
        {
            try
            {
                return std::stoi(parameter.value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        void EnsureTarget(std::unique_ptr<RenderTarget> &target, int width, int height)
        {
            if (!target)
            {
                target = std::make_unique<RenderTarget>(RenderTargetConfig{
                    .width = width,
                    .height = height,
                    .clearColor = glm::vec4(0.0f),
                });
                return;
            }

            if (target->GetWidth() != width || target->GetHeight() != height)
            {
                target->Resize(width, height);
            }
        }
    }

    std::vector<PostProcessParameter> BloomEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Intensity", .type = PostProcessParameterType::Float, .value = std::to_string(m_intensity)},
            PostProcessParameter{.name = "Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_threshold)},
            PostProcessParameter{.name = "Soft Knee", .type = PostProcessParameterType::Float, .value = std::to_string(m_softKnee)},
            PostProcessParameter{.name = "Radius", .type = PostProcessParameterType::Float, .value = std::to_string(m_radius)},
            PostProcessParameter{.name = "Iterations", .type = PostProcessParameterType::Int, .value = std::to_string(m_iterations)},
        };
    }

    void BloomEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Intensity")
            {
                m_intensity = std::max(0.0f, ReadFloatParameter(parameter, m_intensity));
            }
            else if (parameter.name == "Threshold")
            {
                m_threshold = std::max(0.0f, ReadFloatParameter(parameter, m_threshold));
            }
            else if (parameter.name == "Soft Knee")
            {
                m_softKnee = std::clamp(ReadFloatParameter(parameter, m_softKnee), 0.0f, 1.0f);
            }
            else if (parameter.name == "Radius")
            {
                m_radius = std::clamp(ReadFloatParameter(parameter, m_radius), 0.25f, 2.5f);
            }
            else if (parameter.name == "Iterations")
            {
                m_iterations = std::clamp(ReadIntParameter(parameter, m_iterations), 1, 8);
            }
        }
    }

    void BloomEffect::Initialize()
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

        ShaderSource prefilterSource = source;
        prefilterSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSourceTexture;
            uniform vec2 uSourceTexelSize;
            uniform float uThreshold;
            uniform float uSoftKnee;

            vec3 SampleTent9(vec2 uv, vec2 texelSize)
            {
                vec3 center = texture(uSourceTexture, uv).rgb * 4.0;
                vec3 cross =
                    texture(uSourceTexture, uv + vec2(-texelSize.x, 0.0)).rgb * 2.0 +
                    texture(uSourceTexture, uv + vec2(texelSize.x, 0.0)).rgb * 2.0 +
                    texture(uSourceTexture, uv + vec2(0.0, -texelSize.y)).rgb * 2.0 +
                    texture(uSourceTexture, uv + vec2(0.0, texelSize.y)).rgb * 2.0;
                vec3 corners =
                    texture(uSourceTexture, uv + vec2(-texelSize.x, -texelSize.y)).rgb +
                    texture(uSourceTexture, uv + vec2(texelSize.x, -texelSize.y)).rgb +
                    texture(uSourceTexture, uv + vec2(-texelSize.x, texelSize.y)).rgb +
                    texture(uSourceTexture, uv + vec2(texelSize.x, texelSize.y)).rgb;
                return max((center + cross + corners) * (1.0 / 16.0), vec3(0.0));
            }

            float ComputeBrightness(vec3 color)
            {
                return max(max(color.r, color.g), color.b);
            }

            vec3 ApplyBloomThreshold(vec3 color)
            {
                float brightness = ComputeBrightness(color);
                float knee = max(uThreshold * uSoftKnee, 0.0001);
                float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
                soft = (soft * soft) / max(4.0 * knee + 0.00001, 0.00001);
                float contribution = max(soft, brightness - uThreshold) / max(brightness, 0.00001);
                return color * max(contribution, 0.0);
            }

            void main()
            {
                vec3 color = SampleTent9(UV, uSourceTexelSize);
                FragColor = vec4(ApplyBloomThreshold(color), 1.0);
            }
        )";

        ShaderSource downsampleSource = source;
        downsampleSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSourceTexture;
            uniform vec2 uSourceTexelSize;

            vec3 SampleTent9(vec2 uv, vec2 texelSize)
            {
                vec3 center = texture(uSourceTexture, uv).rgb * 4.0;
                vec3 cross =
                    texture(uSourceTexture, uv + vec2(-texelSize.x, 0.0)).rgb * 2.0 +
                    texture(uSourceTexture, uv + vec2(texelSize.x, 0.0)).rgb * 2.0 +
                    texture(uSourceTexture, uv + vec2(0.0, -texelSize.y)).rgb * 2.0 +
                    texture(uSourceTexture, uv + vec2(0.0, texelSize.y)).rgb * 2.0;
                vec3 corners =
                    texture(uSourceTexture, uv + vec2(-texelSize.x, -texelSize.y)).rgb +
                    texture(uSourceTexture, uv + vec2(texelSize.x, -texelSize.y)).rgb +
                    texture(uSourceTexture, uv + vec2(-texelSize.x, texelSize.y)).rgb +
                    texture(uSourceTexture, uv + vec2(texelSize.x, texelSize.y)).rgb;
                return max((center + cross + corners) * (1.0 / 16.0), vec3(0.0));
            }

            void main()
            {
                FragColor = vec4(SampleTent9(UV, uSourceTexelSize), 1.0);
            }
        )";

        ShaderSource copySource = source;
        copySource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSourceTexture;

            void main()
            {
                FragColor = vec4(max(texture(uSourceTexture, UV).rgb, vec3(0.0)), 1.0);
            }
        )";

        ShaderSource upsampleSource = source;
        upsampleSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uBaseTexture;
            uniform sampler2D uBloomTexture;
            uniform vec2 uBloomTexelSize;
            uniform float uRadius;

            vec3 SampleTent9(sampler2D textureSampler, vec2 uv, vec2 texelSize, float radius)
            {
                vec2 sampleTexel = texelSize * max(radius, 0.0001);
                vec3 center = texture(textureSampler, uv).rgb * 4.0;
                vec3 cross =
                    texture(textureSampler, uv + vec2(-sampleTexel.x, 0.0)).rgb * 2.0 +
                    texture(textureSampler, uv + vec2(sampleTexel.x, 0.0)).rgb * 2.0 +
                    texture(textureSampler, uv + vec2(0.0, -sampleTexel.y)).rgb * 2.0 +
                    texture(textureSampler, uv + vec2(0.0, sampleTexel.y)).rgb * 2.0;
                vec3 corners =
                    texture(textureSampler, uv + vec2(-sampleTexel.x, -sampleTexel.y)).rgb +
                    texture(textureSampler, uv + vec2(sampleTexel.x, -sampleTexel.y)).rgb +
                    texture(textureSampler, uv + vec2(-sampleTexel.x, sampleTexel.y)).rgb +
                    texture(textureSampler, uv + vec2(sampleTexel.x, sampleTexel.y)).rgb;
                return max((center + cross + corners) * (1.0 / 16.0), vec3(0.0));
            }

            void main()
            {
                vec3 baseColor = max(texture(uBaseTexture, UV).rgb, vec3(0.0));
                vec3 bloomColor = SampleTent9(uBloomTexture, UV, uBloomTexelSize, uRadius);
                FragColor = vec4(baseColor + bloomColor, 1.0);
            }
        )";

        ShaderSource compositeSource = source;
        compositeSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uBloomTexture;
            uniform float uIntensity;

            void main()
            {
                vec4 source = texture(uSceneTexture, UV);
                vec3 bloom = max(texture(uBloomTexture, UV).rgb, vec3(0.0)) * max(uIntensity, 0.0);
                FragColor = vec4(max(source.rgb + bloom, vec3(0.0)), source.a);
            }
        )";

        m_prefilterShader = Shader::Create(prefilterSource);
        m_downsampleShader = Shader::Create(downsampleSource);
        m_copyShader = Shader::Create(copySource);
        m_upsampleShader = Shader::Create(upsampleSource);
        m_compositeShader = Shader::Create(compositeSource);
    }

    void BloomEffect::EnsurePyramid(int width, int height)
    {
        int currentWidth = std::max(width / 2, 1);
        int currentHeight = std::max(height / 2, 1);
        m_activeMipCount = 0;

        for (int level = 0; level < std::clamp(m_iterations, 1, 8); ++level)
        {
            if (static_cast<int>(m_downsampleTargets.size()) <= level)
            {
                m_downsampleTargets.emplace_back();
            }
            if (static_cast<int>(m_upsampleTargets.size()) <= level)
            {
                m_upsampleTargets.emplace_back();
            }

            EnsureTarget(m_downsampleTargets[static_cast<std::size_t>(level)], currentWidth, currentHeight);
            EnsureTarget(m_upsampleTargets[static_cast<std::size_t>(level)], currentWidth, currentHeight);
            ++m_activeMipCount;

            const int nextWidth = std::max(currentWidth / 2, 1);
            const int nextHeight = std::max(currentHeight / 2, 1);
            if (nextWidth == currentWidth && nextHeight == currentHeight)
            {
                break;
            }

            currentWidth = nextWidth;
            currentHeight = nextHeight;
        }

        m_downsampleTargets.resize(static_cast<std::size_t>(m_activeMipCount));
        m_upsampleTargets.resize(static_cast<std::size_t>(m_activeMipCount));
    }

    void BloomEffect::RenderPrefilter(GLuint sourceTexture, int sourceWidth, int sourceHeight, RenderTarget &destination)
    {
        Graphics::BindRenderTarget(&destination);
        Graphics::SetViewport(0, 0, destination.GetWidth(), destination.GetHeight());
        Graphics::Disable(GL_BLEND);
        m_prefilterShader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0 + kSourceTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, sourceTexture);
        m_prefilterShader->SetUniform("uSourceTexture", kSourceTextureSlot);
        m_prefilterShader->SetUniform("uSourceTexelSize", glm::vec2(1.0f / std::max(sourceWidth, 1), 1.0f / std::max(sourceHeight, 1)));
        m_prefilterShader->SetUniform("uThreshold", std::max(m_threshold, 0.0f));
        m_prefilterShader->SetUniform("uSoftKnee", std::clamp(m_softKnee, 0.0f, 1.0f));
        DrawFullscreenTriangle();
    }

    void BloomEffect::RenderDownsample(GLuint sourceTexture, int sourceWidth, int sourceHeight, RenderTarget &destination)
    {
        Graphics::BindRenderTarget(&destination);
        Graphics::SetViewport(0, 0, destination.GetWidth(), destination.GetHeight());
        Graphics::Disable(GL_BLEND);
        m_downsampleShader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0 + kSourceTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, sourceTexture);
        m_downsampleShader->SetUniform("uSourceTexture", kSourceTextureSlot);
        m_downsampleShader->SetUniform("uSourceTexelSize", glm::vec2(1.0f / std::max(sourceWidth, 1), 1.0f / std::max(sourceHeight, 1)));
        DrawFullscreenTriangle();
    }

    void BloomEffect::RenderCopy(GLuint sourceTexture, RenderTarget &destination)
    {
        Graphics::BindRenderTarget(&destination);
        Graphics::SetViewport(0, 0, destination.GetWidth(), destination.GetHeight());
        Graphics::Disable(GL_BLEND);
        m_copyShader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0 + kSourceTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, sourceTexture);
        m_copyShader->SetUniform("uSourceTexture", kSourceTextureSlot);
        DrawFullscreenTriangle();
    }

    void BloomEffect::RenderUpsample(GLuint baseTexture, GLuint bloomTexture, int bloomWidth, int bloomHeight, RenderTarget &destination)
    {
        Graphics::BindRenderTarget(&destination);
        Graphics::SetViewport(0, 0, destination.GetWidth(), destination.GetHeight());
        Graphics::Disable(GL_BLEND);
        m_upsampleShader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0 + kBaseTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, baseTexture);
        m_upsampleShader->SetUniform("uBaseTexture", kBaseTextureSlot);
        Graphics::ActiveTexture(GL_TEXTURE0 + kBloomTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, bloomTexture);
        m_upsampleShader->SetUniform("uBloomTexture", kBloomTextureSlot);
        m_upsampleShader->SetUniform("uBloomTexelSize", glm::vec2(1.0f / std::max(bloomWidth, 1), 1.0f / std::max(bloomHeight, 1)));
        m_upsampleShader->SetUniform("uRadius", std::clamp(m_radius, 0.25f, 2.5f));
        DrawFullscreenTriangle();
    }

    void BloomEffect::Apply(const PostProcessContext &context)
    {
        if (!m_prefilterShader || !m_downsampleShader || !m_copyShader || !m_upsampleShader || !m_compositeShader || !context.sourceRenderTarget)
        {
            return;
        }

        EnsurePyramid(context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight());
        if (m_activeMipCount <= 0)
        {
            return;
        }

        RenderPrefilter(
            context.sourceRenderTarget->GetColorTextureID(),
            context.sourceRenderTarget->GetWidth(),
            context.sourceRenderTarget->GetHeight(),
            *m_downsampleTargets[0]);

        for (int level = 1; level < m_activeMipCount; ++level)
        {
            RenderDownsample(
                m_downsampleTargets[static_cast<std::size_t>(level - 1)]->GetColorTextureID(),
                m_downsampleTargets[static_cast<std::size_t>(level - 1)]->GetWidth(),
                m_downsampleTargets[static_cast<std::size_t>(level - 1)]->GetHeight(),
                *m_downsampleTargets[static_cast<std::size_t>(level)]);
        }

        RenderCopy(
            m_downsampleTargets[static_cast<std::size_t>(m_activeMipCount - 1)]->GetColorTextureID(),
            *m_upsampleTargets[static_cast<std::size_t>(m_activeMipCount - 1)]);

        for (int level = m_activeMipCount - 2; level >= 0; --level)
        {
            RenderUpsample(
                m_downsampleTargets[static_cast<std::size_t>(level)]->GetColorTextureID(),
                m_upsampleTargets[static_cast<std::size_t>(level + 1)]->GetColorTextureID(),
                m_upsampleTargets[static_cast<std::size_t>(level + 1)]->GetWidth(),
                m_upsampleTargets[static_cast<std::size_t>(level + 1)]->GetHeight(),
                *m_upsampleTargets[static_cast<std::size_t>(level)]);
        }

        BeginApply(context);
        Graphics::Disable(GL_BLEND);
        m_compositeShader->Bind();
        BindCommonInputs(m_compositeShader, context);
        Graphics::ActiveTexture(GL_TEXTURE0 + kCompositeBloomTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, m_upsampleTargets[0]->GetColorTextureID());
        m_compositeShader->SetUniform("uBloomTexture", kCompositeBloomTextureSlot);
        m_compositeShader->SetUniform("uIntensity", std::max(m_intensity, 0.0f));
        DrawFullscreenTriangle();
        EndApply();
    }
}
