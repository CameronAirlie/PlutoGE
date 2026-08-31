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

    LensFlareEffect::LensFlareEffect() = default;

    LensFlareEffect::~LensFlareEffect()
    {
        if (m_brightTarget)
        {
            m_brightTarget->Cleanup();
        }
        if (m_flareTarget)
        {
            m_flareTarget->Cleanup();
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
                vec2 center = vec2(0.5);
                vec3 flare = vec3(0.0);

                // Each output pixel traces back toward a bright source along the
                // optical axis. Irregular spacing avoids the synthetic row of
                // identical blobs common to simple screen-space flare shaders.
                const float ghostPositions[5] = float[5](-0.32, 0.28, 0.72, 1.18, 1.72);
                const float ghostWeights[5] = float[5](0.34, 0.62, 0.42, 0.24, 0.12);
                const float ghostSizes[5] = float[5](0.72, 0.38, 0.56, 0.30, 0.22);
                vec2 outputAxis = UV - center;
                for (int index = 0; index < 5; ++index)
                {
                    float position = ghostPositions[index] * mix(0.65, 1.25, uGhostDispersal * 0.5);
                    vec2 ghostUv = center - outputAxis / position;
                    vec2 chroma = normalize(outputAxis + vec2(0.0001)) * uTexelSize * (1.0 + float(index) * 0.45);
                    vec3 bright = vec3(BrightSample(ghostUv + chroma).r,
                                       BrightSample(ghostUv).g,
                                       BrightSample(ghostUv - chroma).b);
                    vec2 shaped = outputAxis * vec2(uAspectRatio, 1.0);
                    float vignette = 1.0 - smoothstep(0.45, 0.95, length(shaped));
                    vec3 mask = FlareMask(center + outputAxis / max(ghostSizes[index], 0.05), uScale);
                    flare += bright * mask * ghostWeights[index] * vignette;
                }

                vec2 axis = outputAxis * vec2(uAspectRatio, 1.0);
                float radius = length(axis);
                vec2 haloUv = center - outputAxis * (0.82 + uGhostDispersal * 0.18);
                float halo = exp(-pow((radius - 0.22 * uScale) / max(0.045 * uScale, 0.012), 2.0));
                vec3 haloColor = vec3(BrightSample(haloUv + vec2(uTexelSize.x * 1.5, 0.0)).r,
                                      BrightSample(haloUv).g,
                                      BrightSample(haloUv - vec2(uTexelSize.x * 1.5, 0.0)).b);
                flare += haloColor * halo * 0.28;

                // Battlefield-style horizontal anamorphic glare: long enough to
                // read as lens streaking, but energy-normalised to avoid a wash.
                vec3 glare = vec3(0.0);
                float glareWeight = 0.0;
                for (int tap = -8; tap <= 8; ++tap)
                {
                    float x = float(tap);
                    float weight = exp(-abs(x) * 0.38);
                    glare += BrightSample(UV + vec2(x * uTexelSize.x * 12.0, 0.0)) * weight;
                    glareWeight += weight;
                }
                glare /= max(glareWeight, 0.001);
                float verticalCore = exp(-abs(outputAxis.y) / max(uTexelSize.y * 80.0, 0.012));
                flare += glare * vec3(0.58, 0.72, 1.0) * (0.16 + verticalCore * 0.10);

                // Compress extreme flare energy before the additive composite;
                // this keeps the source hot without flattening the whole frame.
                flare = flare / (vec3(1.0) + flare * 0.35);
                FragColor = vec4(max(flare * uIntensity, vec3(0.0)), 1.0);
            }
        )";

        ShaderSource compositeSource;
        compositeSource.vertexSource = source.vertexSource;
        compositeSource.fragmentSource = R"(
            #version 330 core
            in vec2 UV;
            out vec4 FragColor;
            uniform sampler2D uSceneTexture;
            uniform sampler2D uFlareTexture;
            void main()
            {
                vec4 scene = texture(uSceneTexture, UV);
                vec3 flare = texture(uFlareTexture, UV).rgb;
                FragColor = vec4(max(scene.rgb + flare, vec3(0.0)), scene.a);
            }
        )";

        m_brightPassShader = Shader::Create(brightPassSource);
        m_shader = Shader::Create(source);
        m_compositeShader = Shader::Create(compositeSource);
    }

    void LensFlareEffect::EnsureEffectTargets(int width, int height)
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

        if (!m_flareTarget)
        {
            m_flareTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = width,
                .height = height,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_flareTarget->GetWidth() != width || m_flareTarget->GetHeight() != height)
        {
            m_flareTarget->Resize(width, height);
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
        if (!m_shader || !m_brightPassShader || !m_compositeShader ||
            !context.sourceRenderTarget || !context.destinationRenderTarget)
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
        // Flare energy is intentionally broad and sampled many times by the
        // full-resolution composite. Extract it at half resolution: this is a
        // stable prefilter and cuts bright-pass bandwidth by 75%.
        const int brightWidth = std::max(1, (targetWidth + 1) / 2);
        const int brightHeight = std::max(1, (targetHeight + 1) / 2);
        EnsureEffectTargets(brightWidth, brightHeight);
        if (!m_brightTarget || !m_brightTarget->IsInitialized() ||
            !m_flareTarget || !m_flareTarget->IsInitialized())
        {
            return;
        }

        // Evaluate the stabilising cross filter and soft threshold once per
        // source pixel, then reuse it for every ghost, halo, and glare tap.
        Graphics::BindRenderTarget(m_brightTarget.get());
        Graphics::SetViewport(0, 0, brightWidth, brightHeight);
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

        // Ghosts, halo, and glare are all deliberately broad, band-limited
        // signals. Generate them beside the bright prefilter rather than paying
        // their 30+ texture taps at every output pixel.
        Graphics::BindRenderTarget(m_flareTarget.get());
        Graphics::SetViewport(0, 0, brightWidth, brightHeight);
        Graphics::Disable(GL_BLEND);
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
        m_shader->SetUniform("uTexelSize", glm::vec2(1.0f / brightWidth, 1.0f / brightHeight));
        m_shader->SetUniform("uAspectRatio", width / height);
        DrawFullscreenTriangle();

        // Preserve a native-resolution destination and source alpha. The final
        // pass is now just one scene read, one bilinearly reconstructed flare
        // read, and one write.
        BeginApply(context);
        m_compositeShader->Bind();
        BindCommonInputs(m_compositeShader, context);
        Graphics::ActiveTexture(GL_TEXTURE5);
        Graphics::BindTexture(GL_TEXTURE_2D, m_flareTarget->GetColorTextureID());
        m_compositeShader->SetUniform("uFlareTexture", 5);
        DrawFullscreenTriangle();

        EndApply();
    }
}
