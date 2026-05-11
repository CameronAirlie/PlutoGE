#include "PlutoGE/render/postprocess/SSAOEffect.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
#include <array>
#include <random>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kCompositeMode = 0;
        constexpr int kAoOnlyMode = 1;
        constexpr int kMaxBlurRadius = 4;
        constexpr int kNoiseTextureSize = 4;

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }

        glm::vec4 EncodeNoiseVector(const glm::vec3 &vector)
        {
            return glm::vec4(vector * 0.5f + 0.5f, 1.0f);
        }
    }

    SSAOEffect::~SSAOEffect()
    {
        if (m_noiseTexture != 0)
        {
            glDeleteTextures(1, &m_noiseTexture);
            m_noiseTexture = 0;
        }
    }

    std::vector<PostProcessParameter> SSAOEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Radius",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_radius),
            },
            PostProcessParameter{
                .name = "Bias",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_bias),
            },
            PostProcessParameter{
                .name = "Intensity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_intensity),
            },
            PostProcessParameter{
                .name = "Power",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_power),
            },
            PostProcessParameter{
                .name = "Temporal Blend",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_temporalBlend),
            },
            PostProcessParameter{
                .name = "History Depth Threshold",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_historyDepthThreshold),
            },
            PostProcessParameter{
                .name = "History Normal Threshold",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_historyNormalThreshold),
            },
            PostProcessParameter{
                .name = "Samples",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_sampleCount),
            },
            PostProcessParameter{
                .name = "Blur Radius",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_blurRadius),
            },
            PostProcessParameter{
                .name = "Half Resolution",
                .type = PostProcessParameterType::Bool,
                .value = m_halfResolution ? "true" : "false",
            },
            PostProcessParameter{
                .name = "AO Only",
                .type = PostProcessParameterType::Bool,
                .value = m_outputMode == kAoOnlyMode ? "true" : "false",
            },
        };
    }

    void SSAOEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Radius")
            {
                m_radius = std::max(0.05f, std::stof(parameter.value));
            }
            else if (parameter.name == "Bias")
            {
                m_bias = std::clamp(std::stof(parameter.value), 0.0f, 0.5f);
            }
            else if (parameter.name == "Intensity")
            {
                m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Power")
            {
                m_power = std::clamp(std::stof(parameter.value), 0.25f, 4.0f);
            }
            else if (parameter.name == "Temporal Blend")
            {
                m_temporalBlend = std::clamp(std::stof(parameter.value), 0.0f, 0.98f);
            }
            else if (parameter.name == "History Depth Threshold")
            {
                m_historyDepthThreshold = std::clamp(std::stof(parameter.value), 0.001f, 0.25f);
            }
            else if (parameter.name == "History Normal Threshold")
            {
                m_historyNormalThreshold = std::clamp(std::stof(parameter.value), 0.0f, 0.999f);
            }
            else if (parameter.name == "Samples")
            {
                m_sampleCount = std::clamp(std::stoi(parameter.value), 4, kKernelSize);
            }
            else if (parameter.name == "Blur Radius")
            {
                m_blurRadius = std::clamp(std::stoi(parameter.value), 0, kMaxBlurRadius);
            }
            else if (parameter.name == "Half Resolution")
            {
                const bool nextHalfResolution = ParseBool(parameter.value);
                if (m_halfResolution != nextHalfResolution)
                {
                    m_halfResolution = nextHalfResolution;
                    ResetHistory();
                }
            }
            else if (parameter.name == "AO Only")
            {
                m_outputMode = ParseBool(parameter.value) ? kAoOnlyMode : kCompositeMode;
            }
        }
    }

    void SSAOEffect::Initialize()
    {
        GenerateKernel();
        GenerateNoiseTexture();

        ShaderSource ssaoSource;

        ssaoSource.vertexSource = R"(
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

        ssaoSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uNoiseTexture;
            uniform mat4 uView;
            uniform mat4 uViewProjection;
            uniform vec3 uSamples[32];
            uniform int uSampleCount;
            uniform float uRadius;
            uniform float uBias;
            uniform float uIntensity;
            uniform float uPower;

            void main()
            {
                vec3 fragPos = texture(uScenePositionTexture, UV).rgb;
                vec3 worldNormal = normalize(texture(uSceneNormalTexture, UV).xyz);

                if (dot(worldNormal, worldNormal) < 0.01)
                {
                    FragColor = vec4(1.0);
                    return;
                }

                vec3 fragViewPos = vec3(uView * vec4(fragPos, 1.0));
                vec3 normal = normalize(mat3(uView) * worldNormal);
                mat4 inverseView = inverse(uView);

                vec2 noiseScale = vec2(textureSize(uScenePositionTexture, 0)) / vec2(textureSize(uNoiseTexture, 0));
                vec3 randomVec = normalize(texture(uNoiseTexture, UV * noiseScale).xyz * 2.0 - 1.0);
                vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
                if (dot(tangent, tangent) < 0.0001)
                {
                    tangent = normalize(abs(normal.z) < 0.999 ? cross(normal, vec3(0.0, 0.0, 1.0)) : cross(normal, vec3(0.0, 1.0, 0.0)));
                }
                vec3 bitangent = normalize(cross(normal, tangent));
                mat3 tbn = mat3(tangent, bitangent, normal);

                float occlusion = 0.0;
                for (int sampleIndex = 0; sampleIndex < 32; ++sampleIndex)
                {
                    if (sampleIndex >= uSampleCount)
                    {
                        break;
                    }

                    vec3 sampleViewPos = fragViewPos + (tbn * uSamples[sampleIndex]) * uRadius;
                    vec4 sampleWorldPos = inverseView * vec4(sampleViewPos, 1.0);
                    vec4 clipPos = uViewProjection * sampleWorldPos;
                    if (clipPos.w <= 0.0001)
                    {
                        continue;
                    }

                    vec3 ndc = clipPos.xyz / clipPos.w;
                    vec2 sampleUv = ndc.xy * 0.5 + 0.5;
                    if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0)
                    {
                        continue;
                    }

                    vec3 sceneSampleWorldPos = texture(uScenePositionTexture, sampleUv).rgb;
                    vec3 sceneSampleViewPos = vec3(uView * vec4(sceneSampleWorldPos, 1.0));
                    vec3 sceneDelta = sceneSampleViewPos - fragViewPos;
                    float sceneDistance = length(sceneDelta);
                    if (sceneDistance <= 0.0001)
                    {
                        continue;
                    }

                    float depthDifference = abs(sceneSampleViewPos.z - fragViewPos.z);
                    float rangeWeight = 1.0 - smoothstep(uRadius * 0.25, uRadius * 1.25, depthDifference);
                    if (rangeWeight <= 0.0)
                    {
                        continue;
                    }

                    float horizonWeight = max(dot(normalize(sceneDelta), normal), 0.0);
                    float sampleOccluded = sceneSampleViewPos.z >= sampleViewPos.z + uBias ? 1.0 : 0.0;
                    occlusion += sampleOccluded * rangeWeight * horizonWeight;
                }

                float ao = 1.0 - (occlusion / float(max(uSampleCount, 1))) * uIntensity;
                ao = pow(clamp(ao, 0.0, 1.0), max(uPower, 0.001));
                FragColor = vec4(vec3(ao), 1.0);
            }
        )";

        m_ssaoShader = Shader::Create(ssaoSource);

        ShaderSource resolveSource;

        resolveSource.vertexSource = ssaoSource.vertexSource;
        resolveSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uRawAoTexture;
            uniform sampler2D uHistoryTexture;
            uniform mat4 uView;
            uniform mat4 uPreviousView;
            uniform mat4 uPreviousViewProjection;
            uniform int uBlurRadius;
            uniform int uHasHistory;
            uniform float uTemporalBlend;
            uniform float uHistoryDepthThreshold;
            uniform float uHistoryNormalThreshold;

            vec2 EncodeNormal(vec3 normal)
            {
                normal /= (abs(normal.x) + abs(normal.y) + abs(normal.z));
                if (normal.z < 0.0)
                {
                    normal.xy = (1.0 - abs(normal.yx)) * sign(normal.xy);
                }

                return normal.xy * 0.5 + 0.5;
            }

            vec3 DecodeNormal(vec2 encoded)
            {
                vec2 f = encoded * 2.0 - 1.0;
                vec3 normal = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
                float t = clamp(-normal.z, 0.0, 1.0);
                normal.xy += vec2(normal.x >= 0.0 ? -t : t, normal.y >= 0.0 ? -t : t);
                return normalize(normal);
            }

            float ResolveAo(vec3 centerPos, vec3 centerNormal, out float minAo, out float maxAo)
            {
                vec2 texelSize = 1.0 / vec2(textureSize(uRawAoTexture, 0));
                float aoSum = 0.0;
                float weightSum = 0.0;
                minAo = 1.0;
                maxAo = 0.0;
                float centerViewDepth = -(uView * vec4(centerPos, 1.0)).z;

                for (int offsetY = -4; offsetY <= 4; ++offsetY)
                {
                    if (abs(offsetY) > uBlurRadius)
                    {
                        continue;
                    }

                    for (int offsetX = -4; offsetX <= 4; ++offsetX)
                    {
                        if (abs(offsetX) > uBlurRadius)
                        {
                            continue;
                        }

                        vec2 offset = vec2(float(offsetX), float(offsetY));
                        vec2 sampleUv = clamp(UV + offset * texelSize, vec2(0.0), vec2(1.0));
                        vec3 samplePos = texture(uScenePositionTexture, sampleUv).rgb;
                        vec3 sampleNormal = normalize(texture(uSceneNormalTexture, sampleUv).xyz);
                        float sampleAo = texture(uRawAoTexture, sampleUv).r;
                        float sampleViewDepth = -(uView * vec4(samplePos, 1.0)).z;
                        float depthDelta = abs(sampleViewDepth - centerViewDepth);
                        if (depthDelta > 0.35)
                        {
                            continue;
                        }

                        float spatialWeight = exp(-dot(offset, offset) * 0.35);
                        float positionWeight = 1.0 / (1.0 + length(samplePos - centerPos) * 16.0);
                        float depthWeight = exp(-depthDelta * 24.0);
                        float normalWeight = pow(max(dot(centerNormal, sampleNormal), 0.0), 16.0);
                        float weight = spatialWeight * positionWeight * depthWeight * normalWeight;

                        aoSum += sampleAo * weight;
                        weightSum += weight;
                        minAo = min(minAo, sampleAo);
                        maxAo = max(maxAo, sampleAo);
                    }
                }

                if (weightSum <= 0.0001)
                {
                    float centerAo = texture(uRawAoTexture, UV).r;
                    minAo = centerAo;
                    maxAo = centerAo;
                    return centerAo;
                }

                return aoSum / weightSum;
            }

            void main()
            {
                vec3 centerPos = texture(uScenePositionTexture, UV).rgb;
                vec3 centerNormal = normalize(texture(uSceneNormalTexture, UV).xyz);
                float minAo;
                float maxAo;
                float currentAo = ResolveAo(centerPos, centerNormal, minAo, maxAo);
                float resolvedAo = currentAo;

                if (uHasHistory != 0)
                {
                    vec4 previousClip = uPreviousViewProjection * vec4(centerPos, 1.0);
                    if (previousClip.w > 0.0001)
                    {
                        vec2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
                        if (previousUv.x >= 0.0 && previousUv.x <= 1.0 && previousUv.y >= 0.0 && previousUv.y <= 1.0)
                        {
                            vec4 historySample = texture(uHistoryTexture, previousUv);
                            float previousViewDepth = -(uPreviousView * vec4(centerPos, 1.0)).z;
                            float relativeDepthDelta = abs(historySample.g - previousViewDepth) / max(previousViewDepth, 0.001);
                            vec3 historyNormal = DecodeNormal(historySample.ba);
                            float normalSimilarity = dot(historyNormal, centerNormal);

                            if (relativeDepthDelta <= uHistoryDepthThreshold && normalSimilarity >= uHistoryNormalThreshold)
                            {
                                float historyAo = clamp(historySample.r, minAo, maxAo);
                                resolvedAo = mix(currentAo, historyAo, uTemporalBlend);
                            }
                        }
                    }
                }

                float viewDepth = -(uView * vec4(centerPos, 1.0)).z;
                vec2 encodedNormal = EncodeNormal(centerNormal);
                FragColor = vec4(resolvedAo, viewDepth, encodedNormal.x, encodedNormal.y);
            }
        )";

        m_resolveShader = Shader::Create(resolveSource);

        ShaderSource compositeSource;

        compositeSource.vertexSource = ssaoSource.vertexSource;
        compositeSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uResolvedAoTexture;
            uniform int uOutputMode;

            void main()
            {
                vec3 sceneColor = texture(uSceneTexture, UV).rgb;
                float ao = texture(uResolvedAoTexture, UV).r;

                if (uOutputMode == 1)
                {
                    FragColor = vec4(vec3(ao), 1.0);
                    return;
                }

                FragColor = vec4(sceneColor * ao, 1.0);
            }
        )";

        m_compositeShader = Shader::Create(compositeSource);
    }

    RenderTarget *SSAOEffect::GenerateResolvedAmbientOcclusion(const RenderContext &renderContext, int width, int height)
    {
        if (!m_ssaoShader || !m_resolveShader || !m_compositeShader || !renderContext.gBuffer || width <= 0 || height <= 0)
        {
            return nullptr;
        }

        EnsureInternalTargets(width, height);
        if (!m_rawAoRenderTarget || !m_rawAoRenderTarget->IsInitialized() || !m_historyRenderTargets[0] || !m_historyRenderTargets[1])
        {
            return nullptr;
        }

        const std::uint8_t previousHistoryIndex = m_historyIndex;
        const std::uint8_t resolvedHistoryIndex = static_cast<std::uint8_t>((m_historyIndex + 1) % m_historyRenderTargets.size());
        RenderTarget *previousHistoryTarget = m_historyRenderTargets[previousHistoryIndex].get();
        RenderTarget *resolvedHistoryTarget = m_historyRenderTargets[resolvedHistoryIndex].get();
        if (!previousHistoryTarget || !resolvedHistoryTarget)
        {
            return nullptr;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        const glm::mat4 viewProjection = renderContext.cameraData.projection * renderContext.cameraData.view;
        const PostProcessContext internalContext{
            .renderContext = renderContext,
            .sourceRenderTarget = m_rawAoRenderTarget.get(),
            .destinationRenderTarget = nullptr,
        };

        Graphics::BindRenderTarget(m_rawAoRenderTarget.get());
        glViewport(0, 0, m_internalWidth, m_internalHeight);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_ssaoShader->Bind();
        BindCommonInputs(m_ssaoShader, internalContext);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
        m_ssaoShader->SetUniform("uNoiseTexture", 5);
        m_ssaoShader->SetUniform("uView", renderContext.cameraData.view);
        m_ssaoShader->SetUniform("uViewProjection", viewProjection);
        m_ssaoShader->SetUniform("uSampleCount", m_sampleCount);
        m_ssaoShader->SetUniform("uRadius", m_radius);
        m_ssaoShader->SetUniform("uBias", m_bias);
        m_ssaoShader->SetUniform("uIntensity", m_intensity);
        m_ssaoShader->SetUniform("uPower", m_power);
        for (int index = 0; index < kKernelSize; ++index)
        {
            m_ssaoShader->SetUniform(std::string("uSamples[") + std::to_string(index) + "]", m_kernel[index]);
        }
        DrawFullscreenTriangle();

        Graphics::BindRenderTarget(resolvedHistoryTarget);
        glViewport(0, 0, m_internalWidth, m_internalHeight);
        glClearColor(1.0f, 0.0f, 0.5f, 0.5f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_resolveShader->Bind();
        BindCommonInputs(m_resolveShader, internalContext);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, m_rawAoRenderTarget->GetColorTextureID());
        m_resolveShader->SetUniform("uRawAoTexture", 5);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, previousHistoryTarget->GetColorTextureID());
        m_resolveShader->SetUniform("uHistoryTexture", 6);
        m_resolveShader->SetUniform("uView", renderContext.cameraData.view);
        m_resolveShader->SetUniform("uPreviousView", m_previousView);
        m_resolveShader->SetUniform("uPreviousViewProjection", m_previousViewProjection);
        m_resolveShader->SetUniform("uBlurRadius", m_blurRadius);
        m_resolveShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
        m_resolveShader->SetUniform("uTemporalBlend", m_temporalBlend);
        m_resolveShader->SetUniform("uHistoryDepthThreshold", m_historyDepthThreshold);
        m_resolveShader->SetUniform("uHistoryNormalThreshold", m_historyNormalThreshold);
        DrawFullscreenTriangle();

        m_historyIndex = resolvedHistoryIndex;
        m_previousView = renderContext.cameraData.view;
        m_previousViewProjection = viewProjection;
        m_hasHistory = true;
        return resolvedHistoryTarget;
    }

    void SSAOEffect::RenderResolvedAoTexture(const PostProcessContext &context, RenderTarget *resolvedAoTarget, int outputMode)
    {
        if (!m_compositeShader || !resolvedAoTarget || !context.destinationRenderTarget)
        {
            return;
        }

        BeginApply(context);

        m_compositeShader->Bind();
        BindCommonInputs(m_compositeShader, context);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, resolvedAoTarget->GetColorTextureID());
        m_compositeShader->SetUniform("uResolvedAoTexture", 5);
        m_compositeShader->SetUniform("uOutputMode", outputMode);
        DrawFullscreenTriangle();

        EndApply();
    }

    void SSAOEffect::RenderAmbientOcclusion(const RenderContext &renderContext, RenderTarget *destinationRenderTarget)
    {
        if (!destinationRenderTarget)
        {
            return;
        }

        RenderTarget *resolvedAoTarget = GenerateResolvedAmbientOcclusion(renderContext, destinationRenderTarget->GetWidth(), destinationRenderTarget->GetHeight());
        if (!resolvedAoTarget)
        {
            return;
        }

        RenderResolvedAoTexture(PostProcessContext{
                                    .renderContext = renderContext,
                                    .sourceRenderTarget = destinationRenderTarget,
                                    .destinationRenderTarget = destinationRenderTarget,
                                },
                                resolvedAoTarget,
                                kAoOnlyMode);
    }

    void SSAOEffect::Apply(const PostProcessContext &context)
    {
        if (!context.sourceRenderTarget || !context.destinationRenderTarget)
        {
            return;
        }

        RenderTarget *resolvedAoTarget = GenerateResolvedAmbientOcclusion(context.renderContext, context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight());
        if (!resolvedAoTarget)
        {
            return;
        }

        RenderResolvedAoTexture(context, resolvedAoTarget, m_outputMode);
    }

    void SSAOEffect::EnsureInternalTargets(int width, int height)
    {
        const int targetWidth = std::max(1, m_halfResolution ? width / 2 : width);
        const int targetHeight = std::max(1, m_halfResolution ? height / 2 : height);
        if (targetWidth != m_internalWidth || targetHeight != m_internalHeight)
        {
            m_internalWidth = targetWidth;
            m_internalHeight = targetHeight;
            ResetHistory();
        }

        if (!m_rawAoRenderTarget)
        {
            m_rawAoRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = m_internalWidth,
                .height = m_internalHeight,
                .clearColor = glm::vec4(1.0f),
            });
        }
        else if (m_rawAoRenderTarget->GetWidth() != m_internalWidth || m_rawAoRenderTarget->GetHeight() != m_internalHeight)
        {
            m_rawAoRenderTarget->Resize(m_internalWidth, m_internalHeight);
        }

        for (auto &historyRenderTarget : m_historyRenderTargets)
        {
            if (!historyRenderTarget)
            {
                historyRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                    .width = m_internalWidth,
                    .height = m_internalHeight,
                    .clearColor = glm::vec4(1.0f),
                });
            }
            else if (historyRenderTarget->GetWidth() != m_internalWidth || historyRenderTarget->GetHeight() != m_internalHeight)
            {
                historyRenderTarget->Resize(m_internalWidth, m_internalHeight);
            }
        }
    }

    void SSAOEffect::GenerateKernel()
    {
        std::mt19937 generator(1337u);
        std::uniform_real_distribution<float> randomFloat(0.0f, 1.0f);
        std::uniform_real_distribution<float> randomSigned(-1.0f, 1.0f);

        for (int index = 0; index < kKernelSize; ++index)
        {
            glm::vec3 sample(
                randomSigned(generator),
                randomSigned(generator),
                randomFloat(generator));
            sample = glm::normalize(sample);
            sample *= randomFloat(generator);

            const float scale = static_cast<float>(index) / static_cast<float>(kKernelSize - 1);
            const float lerpedScale = 0.1f + (scale * scale) * 0.9f;
            m_kernel[index] = sample * lerpedScale;
        }
    }

    void SSAOEffect::GenerateNoiseTexture()
    {
        if (m_noiseTexture != 0)
        {
            glDeleteTextures(1, &m_noiseTexture);
            m_noiseTexture = 0;
        }

        std::mt19937 generator(4242u);
        std::uniform_real_distribution<float> randomSigned(-1.0f, 1.0f);
        std::array<glm::vec4, kNoiseTextureSize * kNoiseTextureSize> noiseData{};

        for (glm::vec4 &noiseSample : noiseData)
        {
            const glm::vec3 noiseVector = glm::normalize(glm::vec3(
                randomSigned(generator),
                randomSigned(generator),
                0.0f));
            noiseSample = EncodeNoiseVector(noiseVector);
        }

        glGenTextures(1, &m_noiseTexture);
        glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, kNoiseTextureSize, kNoiseTextureSize, 0, GL_RGBA, GL_FLOAT, noiseData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    void SSAOEffect::ResetHistory()
    {
        m_hasHistory = false;
        m_historyIndex = 0;
    }
}