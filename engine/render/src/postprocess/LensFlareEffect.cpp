#include "PlutoGE/render/postprocess/LensFlareEffect.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/TextureManager.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
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

        std::vector<unsigned char> BuildFallbackFlarePixels(int size)
        {
            std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);
            const float center = (static_cast<float>(size) - 1.0f) * 0.5f;

            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const float dx = (static_cast<float>(x) - center) / center;
                    const float dy = (static_cast<float>(y) - center) / center;
                    const float radius = std::sqrt(dx * dx + dy * dy);
                    const float core = std::max(0.0f, 1.0f - radius * 2.2f);
                    const float ring = std::max(0.0f, 1.0f - std::abs(radius - 0.52f) * 18.0f) * 0.35f;
                    const float streak = std::max(0.0f, 1.0f - std::abs(dy) * 18.0f) * std::max(0.0f, 1.0f - std::abs(dx) * 0.9f) * 0.45f;
                    const float glow = std::clamp(core * core + ring + streak, 0.0f, 1.0f);
                    const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
                    pixels[offset + 0] = static_cast<unsigned char>(std::clamp(glow * 255.0f, 0.0f, 255.0f));
                    pixels[offset + 1] = static_cast<unsigned char>(std::clamp(glow * 220.0f, 0.0f, 255.0f));
                    pixels[offset + 2] = static_cast<unsigned char>(std::clamp(glow * 170.0f, 0.0f, 255.0f));
                    pixels[offset + 3] = static_cast<unsigned char>(std::clamp(glow * 255.0f, 0.0f, 255.0f));
                }
            }

            return pixels;
        }
    }

    LensFlareEffect::~LensFlareEffect()
    {
        if (m_brightTarget)
        {
            m_brightTarget->Cleanup();
        }
    }

    std::vector<PostProcessParameter> LensFlareEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Texture Path",
                .type = PostProcessParameterType::String,
                .value = m_texturePath,
            },
            PostProcessParameter{
                .name = "Intensity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_intensity),
            },
            PostProcessParameter{
                .name = "Threshold",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_threshold),
            },
            PostProcessParameter{
                .name = "Scale",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_scale),
            },
            PostProcessParameter{
                .name = "Ghost Dispersal",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_ghostDispersal),
            },
        };
    }

    void LensFlareEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Texture Path")
            {
                if (m_texturePath != parameter.value)
                {
                    m_texturePath = parameter.value;
                    m_flareTexture = nullptr;
                    m_loadedTexturePath.clear();
                }
            }
            else if (parameter.name == "Intensity")
            {
                m_intensity = std::max(0.0f, ReadFloatParameter(parameter, m_intensity));
            }
            else if (parameter.name == "Threshold")
            {
                m_threshold = std::max(0.0f, ReadFloatParameter(parameter, m_threshold));
            }
            else if (parameter.name == "Scale")
            {
                m_scale = std::max(0.05f, ReadFloatParameter(parameter, m_scale));
            }
            else if (parameter.name == "Ghost Dispersal")
            {
                m_ghostDispersal = std::clamp(ReadFloatParameter(parameter, m_ghostDispersal), 0.0f, 2.0f);
            }
        }
    }

    void LensFlareEffect::Initialize()
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

        ShaderSource brightPassSource;
        brightPassSource.vertexSource = source.vertexSource;
        brightPassSource.fragmentSource = R"(
            #version 330 core
            in vec2 UV;
            out vec4 FragColor;
            uniform sampler2D uSceneTexture;
            uniform vec2 uTexelSize;
            uniform float uThreshold;

            float Luma(vec3 color)
            {
                return dot(color, vec3(0.2126, 0.7152, 0.0722));
            }

            void main()
            {
                vec3 color = texture(uSceneTexture, UV).rgb * 0.5;
                color += texture(uSceneTexture, UV + vec2(uTexelSize.x, 0.0)).rgb * 0.125;
                color += texture(uSceneTexture, UV - vec2(uTexelSize.x, 0.0)).rgb * 0.125;
                color += texture(uSceneTexture, UV + vec2(0.0, uTexelSize.y)).rgb * 0.125;
                color += texture(uSceneTexture, UV - vec2(0.0, uTexelSize.y)).rgb * 0.125;
                color = max(color, vec3(0.0));
                float luminance = Luma(color);
                float knee = max(uThreshold * 0.5, 0.05);
                float soft = clamp(luminance - uThreshold + knee, 0.0, 2.0 * knee);
                soft = soft * soft / (4.0 * knee + 0.0001);
                float contribution = max(luminance - uThreshold, soft) / max(luminance, 0.0001);
                FragColor = vec4(color * contribution, 1.0);
            }
        )";

        source.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uFlareTexture;
            uniform sampler2D uBrightTexture;
            uniform float uIntensity;
            uniform float uScale;
            uniform float uGhostDispersal;
            uniform vec2 uTexelSize;
            uniform float uAspectRatio;

            vec3 BrightSample(vec2 uv)
            {
                if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
                {
                    return vec3(0.0);
                }

                return texture(uBrightTexture, uv).rgb;
            }

            vec3 FlareMask(vec2 uv, float scale)
            {
                vec2 maskUv = (uv - vec2(0.5)) / max(scale, 0.05) + vec2(0.5);
                if (any(lessThan(maskUv, vec2(0.0))) || any(greaterThan(maskUv, vec2(1.0))))
                {
                    return vec3(0.0);
                }

                vec4 mask = texture(uFlareTexture, maskUv);
                return mask.rgb * mask.a;
            }

            void main()
            {
                vec4 source = texture(uSceneTexture, UV);
                vec2 center = vec2(0.5);
                vec3 flare = vec3(0.0);

                // Ghosts lie on the line through the optical centre. Separate
                // channel samples provide restrained lateral chromatic aberration.
                const float ghostWeights[6] = float[6](0.95, 0.72, 0.52, 0.38, 0.27, 0.18);
                for (int index = 0; index < 6; ++index)
                {
                    float offset = (float(index) + 1.0) * uGhostDispersal;
                    vec2 ghostUv = mix(UV, vec2(1.0) - UV, offset);
                    vec2 axis = ghostUv - center;
                    vec2 chroma = normalize(axis + vec2(0.0001)) * uTexelSize * 2.0;
                    vec3 bright = vec3(BrightSample(ghostUv + chroma).r,
                                       BrightSample(ghostUv).g,
                                       BrightSample(ghostUv - chroma).b);
                    vec2 shaped = axis * vec2(uAspectRatio, 1.0);
                    float vignette = smoothstep(0.78, 0.08, length(shaped));
                    vec3 mask = FlareMask(center + axis * (1.0 + float(index) * 0.22), uScale);
                    flare += bright * mask * ghostWeights[index] * vignette;
                }

                vec2 axis = (UV - center) * vec2(uAspectRatio, 1.0);
                float radius = length(axis);
                vec2 haloUv = center - (UV - center) * (0.75 + uGhostDispersal * 0.35);
                float halo = exp(-pow((radius - 0.28 * uScale) / max(0.035 * uScale, 0.01), 2.0));
                flare += BrightSample(haloUv) * halo * 0.35;

                // Subtle anamorphic glare around the brightest source pixels.
                vec3 glare = vec3(0.0);
                for (int tap = -4; tap <= 4; ++tap)
                {
                    float x = float(tap);
                    glare += BrightSample(UV + vec2(x * uTexelSize.x * 5.0, 0.0)) * exp(-abs(x) * 0.65);
                }
                flare += glare * FlareMask(UV, uScale * 1.25) * 0.08;

                vec3 color = source.rgb + flare * uIntensity;
                FragColor = vec4(max(color, vec3(0.0)), source.a);
            }
        )";

        m_brightPassShader = Shader::Create(brightPassSource);
        m_shader = Shader::Create(source);
    }

    void LensFlareEffect::EnsureBrightTarget(int width, int height)
    {
        if (!m_brightTarget)
        {
            m_brightTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = width,
                .height = height,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_brightTarget->GetWidth() != width || m_brightTarget->GetHeight() != height)
        {
            m_brightTarget->Resize(width, height);
        }
    }

    Texture *LensFlareEffect::ResolveFlareTexture()
    {
        if (m_flareTexture && m_loadedTexturePath == m_texturePath)
        {
            return m_flareTexture;
        }

        auto &textureManager = core::Engine::GetInstance().GetTextureManager();
        if (!m_texturePath.empty())
        {
            m_flareTexture = textureManager.LoadTextureFromFile(m_texturePath.c_str());
            if (m_flareTexture)
            {
                m_loadedTexturePath = m_texturePath;
                return m_flareTexture;
            }
        }

        constexpr int kFallbackFlareSize = 128;
        const auto pixels = BuildFallbackFlarePixels(kFallbackFlareSize);
        m_flareTexture = textureManager.LoadTextureFromMemory(
            "__builtin_lens_flare_fallback",
            pixels.data(),
            kFallbackFlareSize,
            kFallbackFlareSize,
            4);
        m_loadedTexturePath = m_texturePath;
        return m_flareTexture;
    }

    void LensFlareEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !m_brightPassShader || !context.sourceRenderTarget)
        {
            return;
        }

        Texture *flareTexture = ResolveFlareTexture();
        if (!flareTexture)
        {
            return;
        }

        const int targetWidth = std::max(context.sourceRenderTarget->GetWidth(), 1);
        const int targetHeight = std::max(context.sourceRenderTarget->GetHeight(), 1);
        EnsureBrightTarget(targetWidth, targetHeight);

        // Evaluate the stabilising cross filter and soft threshold once per
        // source pixel, then reuse it for every ghost, halo, and glare tap.
        Graphics::BindRenderTarget(m_brightTarget.get());
        Graphics::SetViewport(0, 0, targetWidth, targetHeight);
        Graphics::Disable(GL_DEPTH_TEST);
        Graphics::Disable(GL_CULL_FACE);
        Graphics::Disable(GL_BLEND);
        m_brightPassShader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0);
        Graphics::BindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetColorTextureID());
        m_brightPassShader->SetUniform("uSceneTexture", 0);
        m_brightPassShader->SetUniform("uTexelSize", glm::vec2(1.0f / targetWidth, 1.0f / targetHeight));
        m_brightPassShader->SetUniform("uThreshold", m_threshold);
        DrawFullscreenTriangle();

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        Graphics::ActiveTexture(GL_TEXTURE5);
        Graphics::BindTexture(GL_TEXTURE_2D, flareTexture->GetTextureID());
        m_shader->SetUniform("uFlareTexture", 5);
        Graphics::ActiveTexture(GL_TEXTURE6);
        Graphics::BindTexture(GL_TEXTURE_2D, m_brightTarget->GetColorTextureID());
        m_shader->SetUniform("uBrightTexture", 6);
        m_shader->SetUniform("uIntensity", m_intensity);
        m_shader->SetUniform("uScale", m_scale);
        m_shader->SetUniform("uGhostDispersal", m_ghostDispersal);
        const float width = static_cast<float>(targetWidth);
        const float height = static_cast<float>(targetHeight);
        m_shader->SetUniform("uTexelSize", glm::vec2(1.0f / width, 1.0f / height));
        m_shader->SetUniform("uAspectRatio", width / height);
        DrawFullscreenTriangle();

        EndApply();
    }
}
