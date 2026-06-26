#include "PlutoGE/render/postprocess/LensFlareEffect.h"

#include "PlutoGE/core/Engine.h"
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

        source.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uFlareTexture;
            uniform float uIntensity;
            uniform float uThreshold;
            uniform float uScale;
            uniform float uGhostDispersal;

            float Luma(vec3 color)
            {
                return dot(color, vec3(0.2126, 0.7152, 0.0722));
            }

            vec3 BrightSample(vec2 uv)
            {
                if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
                {
                    return vec3(0.0);
                }

                vec3 color = max(texture(uSceneTexture, uv).rgb, vec3(0.0));
                float brightness = max(Luma(color) - uThreshold, 0.0);
                return color * brightness / max(Luma(color), 0.0001);
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
                vec2 direction = center - UV;
                vec3 flare = vec3(0.0);

                const float ghostWeights[5] = float[5](1.0, 0.78, 0.58, 0.42, 0.30);
                for (int index = 0; index < 5; ++index)
                {
                    float stepValue = (float(index) + 1.0) * uGhostDispersal;
                    vec2 sampleUv = UV + direction * stepValue;
                    vec3 bright = BrightSample(sampleUv);
                    vec3 mask = FlareMask(center + (UV - center) * (1.0 + float(index) * 0.28), uScale);
                    flare += bright * mask * ghostWeights[index];
                }

                vec2 mirrorUv = vec2(1.0) - UV;
                flare += BrightSample(mirrorUv) * FlareMask(UV, uScale) * 0.75;

                vec3 color = source.rgb + flare * uIntensity;
                FragColor = vec4(max(color, vec3(0.0)), source.a);
            }
        )";

        m_shader = Shader::Create(source);
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
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        Texture *flareTexture = ResolveFlareTexture();
        if (!flareTexture)
        {
            return;
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, flareTexture->GetTextureID());
        m_shader->SetUniform("uFlareTexture", 5);
        m_shader->SetUniform("uIntensity", m_intensity);
        m_shader->SetUniform("uThreshold", m_threshold);
        m_shader->SetUniform("uScale", m_scale);
        m_shader->SetUniform("uGhostDispersal", m_ghostDispersal);
        DrawFullscreenTriangle();

        EndApply();
    }
}
