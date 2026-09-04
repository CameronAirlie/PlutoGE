#include "PlutoGE/render/postprocess/TAAEffect.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
#include <cmath>
#include <glad/glad.h>

namespace PlutoGE::render
{
    namespace
    {
        float Halton(std::uint64_t index, int base)
        {
            float result = 0.0f;
            float fraction = 1.0f / static_cast<float>(base);
            while (index > 0)
            {
                result += static_cast<float>(index % static_cast<std::uint64_t>(base)) * fraction;
                index /= static_cast<std::uint64_t>(base);
                fraction /= static_cast<float>(base);
            }
            return result;
        }

        bool ParseBoolParameter(const std::string &value)
        {
            return value == "1" || value == "true" || value == "True";
        }
    }

    TAAEffect::TAAEffect(TAAEffectConfig config)
        : m_config(config)
    {
    }

    TAAEffect::~TAAEffect()
    {
        ResetHistory();
    }

    std::vector<PostProcessParameter> TAAEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Quality", .type = PostProcessParameterType::Enum, .value = std::to_string(std::clamp(m_config.quality, 0, 1)), .enumOptions = {"Balanced", "High"}},
            PostProcessParameter{.name = "History Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.historyWeight)},
            PostProcessParameter{.name = "Stationary History Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.stationaryHistoryWeight)},
            PostProcessParameter{.name = "Motion History Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.motionHistoryWeight)},
            PostProcessParameter{.name = "Sharpening", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.sharpening)},
            PostProcessParameter{.name = "Depth Rejection Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.depthRejectionThreshold)},
            PostProcessParameter{.name = "Normal Rejection Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.normalRejectionThreshold)},
            PostProcessParameter{.name = "Velocity Rejection Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.velocityRejectionScale)},
            PostProcessParameter{.name = "Jitter Strength", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.jitterStrength)},
            PostProcessParameter{.name = "Jitter Enabled", .type = PostProcessParameterType::Bool, .value = m_config.jitterEnabled ? "true" : "false"},
            PostProcessParameter{.name = "Jitter Debug", .type = PostProcessParameterType::Bool, .value = m_config.jitterDebug ? "true" : "false"},
        };
    }

    void TAAEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Quality")
            {
                m_config.quality = std::clamp(std::stoi(parameter.value), 0, 1);
            }
            else if (parameter.name == "History Weight")
            {
                m_config.historyWeight = std::clamp(std::stof(parameter.value), 0.0f, 0.99f);
            }
            else if (parameter.name == "Stationary History Weight")
            {
                m_config.stationaryHistoryWeight = std::clamp(std::stof(parameter.value), 0.0f, 0.99f);
            }
            else if (parameter.name == "Motion History Weight")
            {
                m_config.motionHistoryWeight = std::clamp(std::stof(parameter.value), 0.0f, 0.99f);
            }
            else if (parameter.name == "Sharpening")
            {
                m_config.sharpening = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Depth Rejection Threshold")
            {
                m_config.depthRejectionThreshold = std::clamp(std::stof(parameter.value), 0.0f, 0.05f);
            }
            else if (parameter.name == "Normal Rejection Threshold")
            {
                m_config.normalRejectionThreshold = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Velocity Rejection Scale")
            {
                m_config.velocityRejectionScale = std::clamp(std::stof(parameter.value), 0.0f, 400.0f);
            }
            else if (parameter.name == "Jitter Strength")
            {
                m_config.jitterStrength = std::clamp(std::stof(parameter.value), 0.0f, 2.0f);
                ResetHistory();
            }
            else if (parameter.name == "Jitter Enabled")
            {
                m_config.jitterEnabled = ParseBoolParameter(parameter.value);
                ResetHistory();
            }
            else if (parameter.name == "Jitter Debug")
            {
                m_config.jitterDebug = ParseBoolParameter(parameter.value);
                ResetHistory();
            }
        }
    }

    void TAAEffect::Initialize()
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
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uSceneMotionTexture;
            uniform sampler2D uHistoryTexture;
            uniform int uHasHistory;
            uniform int uJitterDebug;
            uniform int uQuality;
            uniform float uHistoryWeight;
            uniform float uStationaryHistoryWeight;
            uniform float uMotionHistoryWeight;
            uniform float uSharpening;
            uniform float uDepthRejectionThreshold;
            uniform float uNormalRejectionThreshold;
            uniform float uVelocityRejectionScale;
            uniform vec2 uCurrentJitterUv;
            uniform vec2 uPreviousJitterUv;

            float Luma(vec3 color)
            {
                return dot(color, vec3(0.299, 0.587, 0.114));
            }

            vec3 Tonemap(vec3 color)
            {
                return color / (max(max(color.r, color.g), color.b) + 1.0);
            }

            vec3 InverseTonemap(vec3 color, float referenceLuma)
            {
                return color / max(1.0 - clamp(max(max(color.r, color.g), color.b), 0.0, 0.999), 0.001);
            }

            vec4 SampleCatmullRom(sampler2D tex, vec2 uv)
            {
                vec2 textureSizeValue = vec2(textureSize(tex, 0));
                vec2 samplePos = uv * textureSizeValue - 0.5;
                vec2 texel = floor(samplePos);
                vec2 f = samplePos - texel;
                vec2 f2 = f * f;
                vec2 f3 = f2 * f;

                vec2 w0 = f2 - 0.5 * (f3 + f);
                vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
                vec2 w3 = 0.5 * (f3 - f2);
                vec2 w2 = 1.0 - w0 - w1 - w3;

                vec2 w12 = w1 + w2;
                vec2 offset12 = w2 / max(w12, vec2(0.0001));
                vec2 invSize = 1.0 / textureSizeValue;

                vec2 uv0 = (texel - 1.0) * invSize;
                vec2 uv12 = (texel + offset12) * invSize;
                vec2 uv3 = (texel + 2.0) * invSize;

                vec4 result = vec4(0.0);
                result += texture(tex, vec2(uv0.x, uv0.y)) * w0.x * w0.y;
                result += texture(tex, vec2(uv12.x, uv0.y)) * w12.x * w0.y;
                result += texture(tex, vec2(uv3.x, uv0.y)) * w3.x * w0.y;
                result += texture(tex, vec2(uv0.x, uv12.y)) * w0.x * w12.y;
                result += texture(tex, vec2(uv12.x, uv12.y)) * w12.x * w12.y;
                result += texture(tex, vec2(uv3.x, uv12.y)) * w3.x * w12.y;
                result += texture(tex, vec2(uv0.x, uv3.y)) * w0.x * w3.y;
                result += texture(tex, vec2(uv12.x, uv3.y)) * w12.x * w3.y;
                result += texture(tex, vec2(uv3.x, uv3.y)) * w3.x * w3.y;
                return result;
            }

            vec3 ClipHistoryToNeighborhood(vec3 historyColor, vec3 currentColor, vec3 minColor, vec3 maxColor)
            {
                vec3 center = 0.5 * (maxColor + minColor);
                vec3 extent = 0.5 * (maxColor - minColor) + vec3(0.0001);
                vec3 offset = historyColor - center;
                vec3 unit = offset / extent;
                float maxUnit = max(max(abs(unit.x), abs(unit.y)), abs(unit.z));
                if (maxUnit > 1.0)
                {
                    return center + offset / maxUnit;
                }
                return historyColor;
            }

            vec2 FindResponsiveMotionVector(vec2 baseUv, vec2 texelSize, out float closestDepth)
            {
                vec2 bestMotion = texture(uSceneMotionTexture, baseUv).xy;
                closestDepth = texture(uSceneDepthTexture, baseUv).r;
                const vec2 offsets[4] = vec2[](
                    vec2(-1.0, 0.0),
                    vec2(1.0, 0.0),
                    vec2(0.0, -1.0),
                    vec2(0.0, 1.0)
                );

                for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
                {
                    vec2 sampleUv = clamp(baseUv + offsets[sampleIndex] * texelSize, vec2(0.0), vec2(1.0));
                    float depth = texture(uSceneDepthTexture, sampleUv).r;
                    // PlutoGE uses reversed-Z, so larger depth values are nearer.
                    // Dilate foreground motion into silhouette pixels; selecting
                    // the farthest neighbour pulls background motion across thin
                    // geometry and prevents stable temporal accumulation there.
                    if (depth > closestDepth)
                    {
                        closestDepth = depth;
                        bestMotion = texture(uSceneMotionTexture, sampleUv).xy;
                    }
                }
                return bestMotion;
            }

            void main()
            {
                vec2 texelSize = 1.0 / vec2(textureSize(uSceneTexture, 0));
                if (uJitterDebug != 0)
                {
                    vec3 raw = texture(uSceneTexture, UV).rgb;
                    if (UV.x < 0.012 || UV.y < 0.018)
                        raw = mix(raw, vec3(1.0, 0.0, 1.0), 0.8);
                    FragColor = vec4(raw, texture(uSceneDepthTexture, UV).r);
                    return;
                }
                vec2 currentUv = clamp(UV - uCurrentJitterUv, vec2(0.0), vec2(1.0));
                vec3 current = texture(uSceneTexture, currentUv).rgb;

                if (uHasHistory == 0)
                {
                    FragColor = vec4(current, 1.0);
                    return;
                }

                float closestDepth = 0.0;
                vec2 motion = FindResponsiveMotionVector(currentUv, texelSize, closestDepth);
                // Motion is generated from jittered view-projection matrices.
                // PlutoGE's projection convention contributes the negative of
                // (current jitter - previous jitter) in raster space, so remove
                // that contribution exactly once.
                vec2 unjitteredMotion = motion + uCurrentJitterUv - uPreviousJitterUv;
                // The history target is already an unjittered resolve. Reproject
                // from the output pixel; currentUv only addresses this frame's
                // jittered source image.
                vec2 historyUv = UV - unjitteredMotion;
                float unjitteredMotionPixels = length(unjitteredMotion / texelSize);
                if (any(lessThan(historyUv, vec2(0.0))) || any(greaterThan(historyUv, vec2(1.0))))
                {
                    FragColor = vec4(current, 1.0);
                    return;
                }

                vec3 history = uQuality > 0 ? SampleCatmullRom(uHistoryTexture, historyUv).rgb : texture(uHistoryTexture, historyUv).rgb;

                vec3 minColor = vec3(1.0e20);
                vec3 maxColor = vec3(-1.0e20);
                vec3 minHdrColor = vec3(1.0e20);
                vec3 maxHdrColor = vec3(-1.0e20);
                vec3 moment1 = vec3(0.0);
                vec3 moment2 = vec3(0.0);
                vec3 spatial = vec3(0.0);
                float spatialWeight = 0.0;
                float centerLuma = Luma(current);
                float minLuma = 1.0e20;
                float maxLuma = -1.0e20;

                for (int y = -1; y <= 1; ++y)
                {
                    for (int x = -1; x <= 1; ++x)
                    {
                        vec2 sampleUv = clamp(currentUv + vec2(x, y) * texelSize, vec2(0.0), vec2(1.0));
                        vec3 sampleColor = texture(uSceneTexture, sampleUv).rgb;
                        vec3 mapped = Tonemap(sampleColor);
                        minColor = min(minColor, mapped);
                        maxColor = max(maxColor, mapped);
                        minHdrColor = min(minHdrColor, sampleColor);
                        maxHdrColor = max(maxHdrColor, sampleColor);
                        moment1 += mapped;
                        moment2 += mapped * mapped;

                        float sampleLuma = Luma(sampleColor);
                        minLuma = min(minLuma, sampleLuma);
                        maxLuma = max(maxLuma, sampleLuma);
                        float rangeWeight = exp(-abs(sampleLuma - centerLuma) * 4.0);
                        float kernelWeight = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
                        float weight = kernelWeight * rangeWeight;
                        spatial += sampleColor * weight;
                        spatialWeight += weight;
                    }
                }

                moment1 /= 9.0;
                moment2 /= 9.0;
                vec3 sigma = sqrt(max(moment2 - moment1 * moment1, vec3(0.0)));
                minColor = max(minColor, moment1 - sigma);
                maxColor = min(maxColor, moment1 + sigma);

                vec3 mappedCurrent = Tonemap(current);
                vec3 clippedHistoryMapped = ClipHistoryToNeighborhood(Tonemap(history), mappedCurrent, minColor, maxColor);
                vec3 clippedHistory = InverseTonemap(clippedHistoryMapped, centerLuma);

                vec4 currentNormalRoughness = texture(uSceneNormalTexture, currentUv);
                vec4 historyNormalRoughness = texture(uSceneNormalTexture, historyUv);
                float currentDepth = texture(uSceneDepthTexture, currentUv).r;
                float historyDepth = texture(uSceneDepthTexture, historyUv).r;
                float normalAgreement = dot(normalize(currentNormalRoughness.rgb), normalize(historyNormalRoughness.rgb));
                float depthDelta = abs(currentDepth - historyDepth);

                float historyValidity = 1.0;
                float depthValidity = 1.0 - smoothstep(uDepthRejectionThreshold, uDepthRejectionThreshold * 2.0, depthDelta);
                float normalValidity = smoothstep(uNormalRejectionThreshold - 0.12, uNormalRejectionThreshold, normalAgreement);
                float disocclusionWeight = smoothstep(0.35, 2.0, unjitteredMotionPixels);
                historyValidity *= mix(1.0, depthValidity * normalValidity, disocclusionWeight);
                historyValidity *= clamp(1.0 - length(unjitteredMotion) * uVelocityRejectionScale, 0.0, 1.0);
                // Lighting is not represented by geometry motion vectors. Reject
                // history when a hard shadow edge no longer agrees with the
                // current frame instead of dragging that edge with the camera.
                float historyLumaDelta = abs(Luma(clippedHistory) - centerLuma) / max(centerLuma, 0.05);
                float lightingValidity = 1.0 - smoothstep(0.08, 0.30, historyLumaDelta);
                // Alternating jitter samples naturally differ on stationary
                // sub-pixel edges. Rejecting those samples defeats TAA's spatial
                // supersampling, so apply lighting rejection only as motion makes
                // stale shadow history increasingly likely.
                historyValidity *= mix(1.0, lightingValidity, disocclusionWeight);

                float localContrast = (maxLuma - minLuma) / max(maxLuma, 0.01);
                float detailContrast = length(current - spatial / max(spatialWeight, 0.0001)) / max(centerLuma + 0.05, 0.05);
                float detailHistoryScale = mix(1.0, 0.82, clamp(max(localContrast * 0.75, detailContrast * 0.65), 0.0, 1.0));
                float motionWeight = mix(uStationaryHistoryWeight, uMotionHistoryWeight, clamp(unjitteredMotionPixels * 0.35, 0.0, 1.0));
                float historyWeight = clamp(min(uHistoryWeight, motionWeight) * historyValidity * detailHistoryScale, 0.0, 0.96);
                vec3 resolved = mix(current, clippedHistory, historyWeight);

                if (uSharpening > 0.0)
                {
                    vec3 lowpass = spatial / max(spatialWeight, 0.0001);
                    vec3 sharpenedCurrent = current + (current - lowpass) * uSharpening;
                    float sharpenDetailMask = 1.0 - smoothstep(0.08, 0.35, localContrast);
                    resolved = mix(resolved, sharpenedCurrent, clamp(uSharpening * sharpenDetailMask * (0.25 + 0.5 * (1.0 - historyWeight)), 0.0, 0.55));
                }

                vec3 hdrExtent = max(maxHdrColor - minHdrColor, vec3(0.001));
                resolved = clamp(resolved, minHdrColor - hdrExtent * 0.35, maxHdrColor + hdrExtent * 0.35);
                FragColor = vec4(max(resolved, vec3(0.0)), 1.0);
            }
        )";

        m_shader = Shader::Create(source);
    }

    CameraData TAAEffect::PrepareCameraData(const CameraData &cameraData, int width, int height, std::uint64_t frameSequence)
    {
        if (width != m_width || height != m_height)
        {
            ResetHistory();
            m_width = width;
            m_height = height;
        }

        CameraData jitteredCameraData = cameraData;
        m_previousJitter = m_currentJitter;
        m_currentJitter = ComputeJitter(frameSequence, width, height);
        if (m_config.jitterEnabled)
        {
            jitteredCameraData.projection[2][0] += m_currentJitter.x;
            jitteredCameraData.projection[2][1] += m_currentJitter.y;
        }
        return jitteredCameraData;
    }

    void TAAEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        const int width = context.sourceRenderTarget->GetWidth();
        const int height = context.sourceRenderTarget->GetHeight();
        if (!context.renderContext.gBuffer || context.renderContext.gBuffer->GetMotionTextureID() == 0)
        {
            ResetHistory();
            BeginApply(context);
            m_shader->Bind();
            BindCommonInputs(m_shader, context);
            m_shader->SetUniform("uHasHistory", 0);
            DrawFullscreenTriangle();
            EndApply();
            return;
        }

        EnsureHistoryTargets(width, height);
        RenderTarget *writeTarget = m_historyTargets[1 - m_historyIndex].get();
        RenderTarget *readTarget = m_historyTargets[m_historyIndex].get();

        Graphics::Disable(GL_DEPTH_TEST);
        Graphics::Disable(GL_CULL_FACE);
        Graphics::BindRenderTarget(writeTarget);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);

        Graphics::ActiveTexture(GL_TEXTURE5);
        Graphics::BindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID());
        m_shader->SetUniform("uSceneMotionTexture", 5);

        Graphics::ActiveTexture(GL_TEXTURE6);
        Graphics::BindTexture(GL_TEXTURE_2D, readTarget ? readTarget->GetColorTextureID() : 0);
        m_shader->SetUniform("uHistoryTexture", 6);

        m_shader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
        m_shader->SetUniform("uJitterDebug", m_config.jitterDebug ? 1 : 0);
        m_shader->SetUniform("uQuality", std::clamp(m_config.quality, 0, 1));
        m_shader->SetUniform("uHistoryWeight", std::clamp(m_config.historyWeight, 0.0f, 0.99f));
        m_shader->SetUniform("uStationaryHistoryWeight", std::clamp(m_config.stationaryHistoryWeight, 0.0f, 0.99f));
        m_shader->SetUniform("uMotionHistoryWeight", std::clamp(m_config.motionHistoryWeight, 0.0f, 0.99f));
        m_shader->SetUniform("uSharpening", std::clamp(m_config.sharpening, 0.0f, 1.0f));
        m_shader->SetUniform("uDepthRejectionThreshold", std::max(m_config.depthRejectionThreshold, 0.00001f));
        m_shader->SetUniform("uNormalRejectionThreshold", std::clamp(m_config.normalRejectionThreshold, 0.0f, 1.0f));
        m_shader->SetUniform("uVelocityRejectionScale", std::max(m_config.velocityRejectionScale, 0.0f));
        m_shader->SetUniform("uCurrentJitterUv", m_currentJitter * 0.5f);
        m_shader->SetUniform("uPreviousJitterUv", m_previousJitter * 0.5f);
        DrawFullscreenTriangle();

        Graphics::UnbindRenderTarget();
        BlitResolvedOutput(writeTarget, context.destinationRenderTarget, context.sourceRenderTarget);

        m_historyIndex = 1 - m_historyIndex;
        m_hasHistory = true;
    }

    void TAAEffect::EnsureHistoryTargets(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        if (width != m_width || height != m_height)
        {
            ResetHistory();
            m_width = width;
            m_height = height;
        }

        for (auto &target : m_historyTargets)
        {
            if (!target)
            {
                target = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height});
            }

            if (!target->IsInitialized() || target->GetWidth() != width || target->GetHeight() != height)
            {
                target->Resize(width, height);
                m_hasHistory = false;
            }
        }
    }

    glm::vec2 TAAEffect::ComputeJitter(std::uint64_t frameSequence, int width, int height) const
    {
        if (!m_config.jitterEnabled || width <= 0 || height <= 0)
        {
            return glm::vec2(0.0f);
        }

        if (m_config.jitterDebug)
        {
            const float direction = (frameSequence & 1u) == 0u ? -1.0f : 1.0f;
            return glm::vec2(direction * 8.0f / static_cast<float>(width),
                             -direction * 8.0f / static_cast<float>(height));
        }

        const std::uint64_t sampleIndex = frameSequence % kJitterSampleCount + 1;
        const float jitterStrength = std::clamp(m_config.jitterStrength, 0.0f, 2.0f);
        const float jitterX = (Halton(sampleIndex, 2) - 0.5f) * 2.0f * jitterStrength / static_cast<float>(width);
        const float jitterY = (Halton(sampleIndex, 3) - 0.5f) * 2.0f * jitterStrength / static_cast<float>(height);
        return glm::vec2(jitterX, jitterY);
    }

    void TAAEffect::BlitResolvedOutput(RenderTarget *source, RenderTarget *destination, RenderTarget *depthSource) const
    {
        if (!source)
        {
            return;
        }

        Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebufferID());
        Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, destination ? destination->GetFramebufferID() : 0);
        glBlitFramebuffer(
            0, 0, source->GetWidth(), source->GetHeight(),
            0, 0, source->GetWidth(), source->GetHeight(),
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST);

        if (destination && depthSource)
        {
            Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, depthSource->GetFramebufferID());
            Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, destination->GetFramebufferID());
            glBlitFramebuffer(
                0, 0, depthSource->GetWidth(), depthSource->GetHeight(),
                0, 0, destination->GetWidth(), destination->GetHeight(),
                GL_DEPTH_BUFFER_BIT,
                GL_NEAREST);
        }

        Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void TAAEffect::ResetHistory()
    {
        m_hasHistory = false;
        m_historyIndex = 0;
        m_currentJitter = glm::vec2(0.0f);
        m_previousJitter = glm::vec2(0.0f);
    }
}
