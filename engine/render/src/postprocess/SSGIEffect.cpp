#include "PlutoGE/render/postprocess/SSGIEffect.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <array>
#include <random>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kCompositeMode = 0;
        constexpr int kIndirectOnlyMode = 1;
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

    SSGIEffect::~SSGIEffect()
    {
        if (m_noiseTexture != 0)
        {
            glDeleteTextures(1, &m_noiseTexture);
            m_noiseTexture = 0;
        }
    }

    std::vector<PostProcessParameter> SSGIEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Intensity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_intensity),
            },
            PostProcessParameter{
                .name = "Ray Distance",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_rayDistance),
            },
            PostProcessParameter{
                .name = "Step Size",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_stepSize),
            },
            PostProcessParameter{
                .name = "Thickness",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_thickness),
            },
            PostProcessParameter{
                .name = "Samples",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_sampleCount),
            },
            PostProcessParameter{
                .name = "Steps",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_stepCount),
            },
            PostProcessParameter{
                .name = "Blur Radius",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_blurRadius),
            },
            PostProcessParameter{
                .name = "Depth Weight",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_depthPhi),
            },
            PostProcessParameter{
                .name = "Normal Weight",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_normalPhi),
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
                .name = "Half Resolution",
                .type = PostProcessParameterType::Bool,
                .value = m_halfResolution ? "true" : "false",
            },
            PostProcessParameter{
                .name = "Indirect Only",
                .type = PostProcessParameterType::Bool,
                .value = m_outputMode == kIndirectOnlyMode ? "true" : "false",
            },
        };
    }

    void SSGIEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Intensity")
            {
                m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Ray Distance")
            {
                m_rayDistance = std::clamp(std::stof(parameter.value), 0.25f, 12.0f);
            }
            else if (parameter.name == "Step Size")
            {
                m_stepSize = std::clamp(std::stof(parameter.value), 0.02f, 2.0f);
            }
            else if (parameter.name == "Thickness")
            {
                m_thickness = std::clamp(std::stof(parameter.value), 0.01f, 2.0f);
            }
            else if (parameter.name == "Samples")
            {
                m_sampleCount = std::clamp(std::stoi(parameter.value), 1, 8);
            }
            else if (parameter.name == "Steps")
            {
                m_stepCount = std::clamp(std::stoi(parameter.value), 4, 32);
            }
            else if (parameter.name == "Blur Radius")
            {
                m_blurRadius = std::clamp(std::stoi(parameter.value), 0, kMaxBlurRadius);
            }
            else if (parameter.name == "Depth Weight")
            {
                m_depthPhi = std::clamp(std::stof(parameter.value), 1.0f, 64.0f);
            }
            else if (parameter.name == "Normal Weight")
            {
                m_normalPhi = std::clamp(std::stof(parameter.value), 1.0f, 32.0f);
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
            else if (parameter.name == "Half Resolution")
            {
                const bool nextHalfResolution = ParseBool(parameter.value);
                if (m_halfResolution != nextHalfResolution)
                {
                    m_halfResolution = nextHalfResolution;
                    ResetHistory();
                }
            }
            else if (parameter.name == "Indirect Only")
            {
                m_outputMode = ParseBool(parameter.value) ? kIndirectOnlyMode : kCompositeMode;
            }
        }
    }

    void SSGIEffect::Initialize()
    {
        GenerateNoiseTexture();

        ShaderSource traceSource;

        traceSource.vertexSource = R"(
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

        traceSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uSceneAlbedoTexture;
            uniform sampler2D uNoiseTexture;
            uniform mat4 uView;
            uniform mat4 uProjection;
            uniform float uIntensity;
            uniform float uMaxRayDistance;
            uniform float uStepSize;
            uniform float uThickness;
            uniform int uSampleCount;
            uniform int uStepCount;
            uniform float uFrameJitter;
            uniform vec3 uDominantLightDirectionView;
            uniform float uDominantLightImportance;
            uniform int uHasDominantDirectionalLight;

            const float PI = 3.14159265359;
            const int MAX_SSGI_SAMPLES = 8;
            const int MAX_SSGI_STEPS = 32;
            const float BOUNCE_BOOST = 1.75;

            vec3 BuildTangent(vec3 normal, vec3 randomVec)
            {
                vec3 tangent = randomVec - normal * dot(randomVec, normal);
                if (dot(tangent, tangent) < 0.0001)
                {
                    tangent = abs(normal.z) < 0.999
                        ? cross(normal, vec3(0.0, 0.0, 1.0))
                        : cross(normal, vec3(0.0, 1.0, 0.0));
                }

                return normalize(tangent);
            }

            vec3 SampleHemisphereDirection(int sampleIndex, int sampleCount, float rotation)
            {
                float sampleU = (float(sampleIndex) + 0.5) / float(max(sampleCount, 1));
                float phi = fract(float(sampleIndex) * 0.61803398875 + rotation) * 2.0 * PI;
                float cosTheta = sqrt(max(1.0 - sampleU, 0.0));
                float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
                return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
            }

            vec3 SampleConeDirection(vec3 axis, int sampleIndex, int sampleCount, float rotation, float coneAngleRadians)
            {
                vec3 safeAxis = normalize(axis);
                vec3 tangent = BuildTangent(safeAxis, vec3(cos(rotation * 2.0 * PI), sin(rotation * 2.0 * PI), 0.37));
                vec3 bitangent = normalize(cross(safeAxis, tangent));

                float sampleU = (float(sampleIndex) + 0.5) / float(max(sampleCount, 1));
                float phi = fract(float(sampleIndex) * 0.75487766 + rotation) * 2.0 * PI;
                float cosTheta = mix(cos(coneAngleRadians), 1.0, sampleU);
                float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
                return normalize(
                    tangent * cos(phi) * sinTheta +
                    bitangent * sin(phi) * sinTheta +
                    safeAxis * cosTheta);
            }

            vec3 BuildDominantBounceDirection(vec3 surfaceNormal, vec3 dominantLightDirectionView)
            {
                vec3 projectedDirection = dominantLightDirectionView - surfaceNormal * dot(dominantLightDirectionView, surfaceNormal);
                if (dot(projectedDirection, projectedDirection) < 0.0001)
                {
                    projectedDirection = surfaceNormal;
                }

                vec3 candidateDirection = normalize(projectedDirection + surfaceNormal * 0.45);
                if (dot(candidateDirection, surfaceNormal) <= 0.0)
                {
                    candidateDirection = surfaceNormal;
                }

                return candidateDirection;
            }

            void main()
            {
                vec3 centerWorldPos = texture(uScenePositionTexture, UV).rgb;
                vec3 centerWorldNormal = normalize(texture(uSceneNormalTexture, UV).xyz);
                vec4 centerAlbedoMetallic = texture(uSceneAlbedoTexture, UV);

                if (dot(centerWorldNormal, centerWorldNormal) < 0.01)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                vec3 centerViewPos = vec3(uView * vec4(centerWorldPos, 1.0));
                vec3 centerViewNormal = normalize(mat3(uView) * centerWorldNormal);
                float centerMetallic = clamp(centerAlbedoMetallic.a, 0.0, 1.0);
                vec3 centerAlbedo = centerAlbedoMetallic.rgb;

                vec2 noiseScale = vec2(textureSize(uScenePositionTexture, 0)) / vec2(textureSize(uNoiseTexture, 0));
                vec2 jitterUv = UV * noiseScale + vec2(uFrameJitter, fract(uFrameJitter * 0.75487766));
                vec3 randomVec = normalize(texture(uNoiseTexture, jitterUv).xyz * 2.0 - 1.0);
                vec3 tangent = BuildTangent(centerViewNormal, randomVec);
                vec3 bitangent = normalize(cross(centerViewNormal, tangent));
                mat3 tbn = mat3(tangent, bitangent, centerViewNormal);

                vec3 indirectRadiance = vec3(0.0);
                float hitWeightSum = 0.0;
                vec3 rayOrigin = centerViewPos + centerViewNormal * max(uThickness * 0.25, 0.025);
                vec3 dominantBounceDirection = BuildDominantBounceDirection(centerViewNormal, uDominantLightDirectionView);
                int importanceSampleCount = uHasDominantDirectionalLight != 0 ? max(uSampleCount / 2, 1) : 0;

                for (int sampleIndex = 0; sampleIndex < MAX_SSGI_SAMPLES; ++sampleIndex)
                {
                    if (sampleIndex >= uSampleCount)
                    {
                        break;
                    }

                    float rotation = fract(uFrameJitter + float(sampleIndex) * 0.137);
                    vec3 sampleDir = normalize(tbn * SampleHemisphereDirection(sampleIndex, uSampleCount, rotation));
                    if (sampleIndex < importanceSampleCount)
                    {
                        vec3 importanceDir = SampleConeDirection(
                            dominantBounceDirection,
                            sampleIndex,
                            importanceSampleCount,
                            rotation,
                            mix(0.55, 0.28, clamp(uDominantLightImportance, 0.0, 1.0)));
                        sampleDir = normalize(mix(sampleDir, importanceDir, 0.8));
                    }

                    float receiverWeight = max(dot(centerViewNormal, sampleDir), 0.0);
                    if (receiverWeight <= 0.001)
                    {
                        continue;
                    }

                    for (int stepIndex = 0; stepIndex < MAX_SSGI_STEPS; ++stepIndex)
                    {
                        if (stepIndex >= uStepCount)
                        {
                            break;
                        }

                        float stepFraction = (float(stepIndex) + 1.0) / float(max(uStepCount, 1));
                        float travel = mix(uStepSize, uMaxRayDistance, stepFraction * stepFraction);
                        if (travel > uMaxRayDistance)
                        {
                            break;
                        }

                        vec3 rayViewPos = rayOrigin + sampleDir * travel;
                        vec4 clipPos = uProjection * vec4(rayViewPos, 1.0);
                        if (clipPos.w <= 0.0001)
                        {
                            continue;
                        }

                        vec2 rayUv = clipPos.xy / clipPos.w * 0.5 + 0.5;
                        if (rayUv.x < 0.0 || rayUv.x > 1.0 || rayUv.y < 0.0 || rayUv.y > 1.0)
                        {
                            break;
                        }

                        vec3 hitWorldPos = texture(uScenePositionTexture, rayUv).rgb;
                        vec3 hitWorldNormal = normalize(texture(uSceneNormalTexture, rayUv).xyz);
                        if (dot(hitWorldNormal, hitWorldNormal) < 0.01)
                        {
                            continue;
                        }

                        vec3 hitViewPos = vec3(uView * vec4(hitWorldPos, 1.0));
                        float rayDepth = -rayViewPos.z;
                        float sceneDepth = -hitViewPos.z;
                        float depthDelta = rayDepth - sceneDepth;
                        float thickness = uThickness * mix(0.85, 2.25, stepFraction);

                        if (depthDelta < 0.0 || depthDelta > thickness)
                        {
                            continue;
                        }

                        float hitDistance = length(hitViewPos - centerViewPos);
                        if (hitDistance <= uStepSize * 0.75)
                        {
                            continue;
                        }

                        vec3 hitViewNormal = normalize(mat3(uView) * hitWorldNormal);
                        float emitterWeight = max(dot(hitViewNormal, -sampleDir), 0.0);
                        if (emitterWeight <= 0.05)
                        {
                            continue;
                        }

                        float distanceWeight = 1.0 - smoothstep(0.0, uMaxRayDistance * 1.5, travel);
                        float attenuation = 1.0 / (1.0 + hitDistance * 0.85);
                        float directionalBias = sampleIndex < importanceSampleCount ? mix(1.0, 1.35, uDominantLightImportance) : 1.0;
                        float hitWeight = max(receiverWeight * emitterWeight * directionalBias, 0.15);
                        vec3 incomingLight = texture(uSceneTexture, rayUv).rgb;
                        indirectRadiance += incomingLight * hitWeight * distanceWeight * attenuation;
                        hitWeightSum += hitWeight;
                        break;
                    }
                }

                if (hitWeightSum > 0.0)
                {
                    indirectRadiance /= hitWeightSum;
                }

                indirectRadiance *= centerAlbedo * (1.0 - centerMetallic) * uIntensity * BOUNCE_BOOST;
                FragColor = vec4(max(indirectRadiance, vec3(0.0)), 1.0);
            }
        )";

        m_traceShader = Shader::Create(traceSource);

        ShaderSource resolveSource;
        resolveSource.vertexSource = traceSource.vertexSource;
        resolveSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uSceneMotionTexture;
            uniform sampler2D uRawIndirectTexture;
            uniform sampler2D uHistoryColorTexture;
            uniform sampler2D uHistoryMetadataTexture;
            uniform mat4 uView;
            uniform mat4 uPreviousView;
            uniform mat4 uPreviousViewProjection;
            uniform int uBlurRadius;
            uniform int uHasHistory;
            uniform float uTemporalBlend;
            uniform float uHistoryDepthThreshold;
            uniform float uHistoryNormalThreshold;
            uniform float uDepthPhi;
            uniform float uNormalPhi;
            uniform float uNearPlane;
            uniform float uFarPlane;

            const int MAX_BLUR_RADIUS = 4;

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

            float NormalizeViewDepth(float viewDepth)
            {
                return clamp((viewDepth - uNearPlane) / max(uFarPlane - uNearPlane, 0.0001), 0.0, 1.0);
            }

            vec3 ResolveIndirect(vec3 centerPos, vec3 centerNormal, out vec3 minIndirect, out vec3 maxIndirect)
            {
                vec2 texelSize = 1.0 / vec2(textureSize(uRawIndirectTexture, 0));
                float centerViewDepth = -(uView * vec4(centerPos, 1.0)).z;
                vec3 indirectSum = vec3(0.0);
                float weightSum = 0.0;
                minIndirect = vec3(1e6);
                maxIndirect = vec3(0.0);

                for (int offsetY = -MAX_BLUR_RADIUS; offsetY <= MAX_BLUR_RADIUS; ++offsetY)
                {
                    if (abs(offsetY) > uBlurRadius)
                    {
                        continue;
                    }

                    for (int offsetX = -MAX_BLUR_RADIUS; offsetX <= MAX_BLUR_RADIUS; ++offsetX)
                    {
                        if (abs(offsetX) > uBlurRadius)
                        {
                            continue;
                        }

                        vec2 offset = vec2(float(offsetX), float(offsetY));
                        vec2 sampleUv = clamp(UV + offset * texelSize, vec2(0.0), vec2(1.0));
                        vec3 samplePos = texture(uScenePositionTexture, sampleUv).rgb;
                        vec3 sampleNormal = normalize(texture(uSceneNormalTexture, sampleUv).xyz);
                        vec3 sampleIndirect = texture(uRawIndirectTexture, sampleUv).rgb;
                        float sampleViewDepth = -(uView * vec4(samplePos, 1.0)).z;
                        float depthDelta = abs(sampleViewDepth - centerViewDepth);
                        float spatialWeight = exp(-dot(offset, offset) * 0.45);
                        float positionWeight = 1.0 / (1.0 + length(samplePos - centerPos) * 10.0);
                        float depthWeight = exp(-depthDelta * uDepthPhi);
                        float normalWeight = pow(max(dot(centerNormal, sampleNormal), 0.0), uNormalPhi);
                        float weight = spatialWeight * positionWeight * depthWeight * max(normalWeight, 0.0001);

                        indirectSum += sampleIndirect * weight;
                        weightSum += weight;
                        minIndirect = min(minIndirect, sampleIndirect);
                        maxIndirect = max(maxIndirect, sampleIndirect);
                    }
                }

                if (weightSum <= 0.0001)
                {
                    vec3 centerIndirect = texture(uRawIndirectTexture, UV).rgb;
                    minIndirect = centerIndirect;
                    maxIndirect = centerIndirect;
                    return centerIndirect;
                }

                return indirectSum / weightSum;
            }

            void main()
            {
                vec3 centerPos = texture(uScenePositionTexture, UV).rgb;
                vec3 centerNormal = normalize(texture(uSceneNormalTexture, UV).xyz);
                if (dot(centerNormal, centerNormal) < 0.01)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                vec3 minIndirect;
                vec3 maxIndirect;
                vec3 currentIndirect = ResolveIndirect(centerPos, centerNormal, minIndirect, maxIndirect);
                vec3 resolvedIndirect = currentIndirect;

                if (uHasHistory != 0)
                {
                    vec2 motionVector = texture(uSceneMotionTexture, UV).xy;
                    vec2 historyUv = UV - motionVector;
                    if (historyUv.x >= 0.0 && historyUv.x <= 1.0 && historyUv.y >= 0.0 && historyUv.y <= 1.0)
                    {
                        vec4 previousClip = uPreviousViewProjection * vec4(centerPos, 1.0);
                        if (previousClip.w > 0.0001)
                        {
                            vec4 historyMetadata = texture(uHistoryMetadataTexture, historyUv);
                            float previousViewDepth = -(uPreviousView * vec4(centerPos, 1.0)).z;
                            float previousViewDepthNorm = NormalizeViewDepth(previousViewDepth);
                            vec3 historyNormal = DecodeNormal(historyMetadata.xy);
                            float normalSimilarity = dot(historyNormal, centerNormal);
                            if (historyMetadata.w > 0.5 &&
                                abs(historyMetadata.z - previousViewDepthNorm) <= uHistoryDepthThreshold &&
                                normalSimilarity >= uHistoryNormalThreshold)
                            {
                                vec3 historySample = texture(uHistoryColorTexture, historyUv).rgb;
                                vec3 historyIndirect = clamp(historySample.rgb, minIndirect, maxIndirect);
                                float motionMagnitude = length(motionVector);
                                float motionWeight = clamp(1.0 - motionMagnitude * 32.0, 0.0, 1.0);
                                float blendWeight = uTemporalBlend * motionWeight;
                                resolvedIndirect = mix(currentIndirect, historyIndirect, blendWeight);
                            }
                        }
                    }
                }

                float currentViewDepth = -(uView * vec4(centerPos, 1.0)).z;
                FragColor = vec4(max(resolvedIndirect, vec3(0.0)), NormalizeViewDepth(currentViewDepth));
            }
        )";

        m_resolveShader = Shader::Create(resolveSource);

        ShaderSource historyMetadataSource;
        historyMetadataSource.vertexSource = traceSource.vertexSource;
        historyMetadataSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform mat4 uView;
            uniform float uNearPlane;
            uniform float uFarPlane;

            vec2 EncodeNormal(vec3 normal)
            {
                normal /= (abs(normal.x) + abs(normal.y) + abs(normal.z));
                if (normal.z < 0.0)
                {
                    normal.xy = (1.0 - abs(normal.yx)) * sign(normal.xy);
                }

                return normal.xy * 0.5 + 0.5;
            }

            float NormalizeViewDepth(float viewDepth)
            {
                return clamp((viewDepth - uNearPlane) / max(uFarPlane - uNearPlane, 0.0001), 0.0, 1.0);
            }

            void main()
            {
                vec3 centerPos = texture(uScenePositionTexture, UV).rgb;
                vec3 centerNormal = normalize(texture(uSceneNormalTexture, UV).xyz);
                if (dot(centerNormal, centerNormal) < 0.01)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                float currentViewDepth = -(uView * vec4(centerPos, 1.0)).z;
                vec2 encodedNormal = EncodeNormal(centerNormal);
                FragColor = vec4(encodedNormal, NormalizeViewDepth(currentViewDepth), 1.0);
            }
        )";

        m_historyMetadataShader = Shader::Create(historyMetadataSource);

        ShaderSource compositeSource;
        compositeSource.vertexSource = traceSource.vertexSource;
        compositeSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uResolvedIndirectTexture;
            uniform int uOutputMode;

            void main()
            {
                vec3 sceneColor = texture(uSceneTexture, UV).rgb;
                vec3 indirectLight = texture(uResolvedIndirectTexture, UV).rgb;

                if (uOutputMode == 1)
                {
                    FragColor = vec4(indirectLight, 1.0);
                    return;
                }

                FragColor = vec4(sceneColor + indirectLight, 1.0);
            }
        )";

        m_compositeShader = Shader::Create(compositeSource);
    }

    RenderTarget *SSGIEffect::GenerateResolvedIndirectLighting(const PostProcessContext &context, int width, int height)
    {
        if (!m_traceShader || !m_resolveShader || !m_historyMetadataShader || !m_compositeShader || !context.renderContext.gBuffer || !context.renderContext.hasCameraData || width <= 0 || height <= 0)
        {
            return nullptr;
        }

        EnsureInternalTargets(width, height);
        if (!m_rawIndirectRenderTarget ||
            !m_rawIndirectRenderTarget->IsInitialized() ||
            !m_historyColorRenderTargets[0] ||
            !m_historyColorRenderTargets[1] ||
            !m_historyMetadataRenderTargets[0] ||
            !m_historyMetadataRenderTargets[1])
        {
            return nullptr;
        }

        const std::uint8_t previousHistoryIndex = m_historyIndex;
        const std::uint8_t resolvedHistoryIndex = static_cast<std::uint8_t>((m_historyIndex + 1) % m_historyColorRenderTargets.size());
        RenderTarget *previousHistoryColorTarget = m_historyColorRenderTargets[previousHistoryIndex].get();
        RenderTarget *resolvedHistoryColorTarget = m_historyColorRenderTargets[resolvedHistoryIndex].get();
        RenderTarget *previousHistoryMetadataTarget = m_historyMetadataRenderTargets[previousHistoryIndex].get();
        RenderTarget *resolvedHistoryMetadataTarget = m_historyMetadataRenderTargets[resolvedHistoryIndex].get();
        if (!previousHistoryColorTarget || !resolvedHistoryColorTarget || !previousHistoryMetadataTarget || !resolvedHistoryMetadataTarget)
        {
            return nullptr;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        const glm::mat4 viewProjection = context.renderContext.cameraData.projection * context.renderContext.cameraData.view;
        glm::vec3 dominantLightDirectionView(0.0f, 0.0f, 1.0f);
        float dominantLightImportance = 0.0f;
        int hasDominantDirectionalLight = 0;
        if (context.renderContext.lights)
        {
            float strongestDirectionalWeight = 0.0f;
            for (const auto *light : *context.renderContext.lights)
            {
                if (!light || light->type != scene::LightType::Directional)
                {
                    continue;
                }

                const float lightWeight = light->intensity * std::max(light->color.x, std::max(light->color.y, light->color.z));
                if (lightWeight <= strongestDirectionalWeight)
                {
                    continue;
                }

                strongestDirectionalWeight = lightWeight;
                dominantLightDirectionView = glm::normalize(glm::mat3(context.renderContext.cameraData.view) * (-light->direction));
                dominantLightImportance = glm::clamp(lightWeight / 6.0f, 0.0f, 1.0f);
                hasDominantDirectionalLight = 1;
            }
        }
        const PostProcessContext internalContext{
            .renderContext = context.renderContext,
            .sourceRenderTarget = context.sourceRenderTarget,
            .destinationRenderTarget = nullptr,
        };

        Graphics::BindRenderTarget(m_rawIndirectRenderTarget.get());
        glViewport(0, 0, m_internalWidth, m_internalHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_traceShader->Bind();
        BindCommonInputs(m_traceShader, internalContext);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
        m_traceShader->SetUniform("uNoiseTexture", 5);
        m_traceShader->SetUniform("uView", context.renderContext.cameraData.view);
        m_traceShader->SetUniform("uProjection", context.renderContext.cameraData.projection);
        m_traceShader->SetUniform("uIntensity", m_intensity);
        m_traceShader->SetUniform("uMaxRayDistance", m_rayDistance);
        m_traceShader->SetUniform("uStepSize", m_stepSize);
        m_traceShader->SetUniform("uThickness", m_thickness);
        m_traceShader->SetUniform("uSampleCount", m_sampleCount);
        m_traceShader->SetUniform("uStepCount", m_stepCount);
        m_traceShader->SetUniform("uFrameJitter", static_cast<float>(context.renderContext.frameSequence % 64ull) / 64.0f);
        m_traceShader->SetUniform("uDominantLightDirectionView", dominantLightDirectionView);
        m_traceShader->SetUniform("uDominantLightImportance", dominantLightImportance);
        m_traceShader->SetUniform("uHasDominantDirectionalLight", hasDominantDirectionalLight);
        DrawFullscreenTriangle();

        Graphics::BindRenderTarget(resolvedHistoryColorTarget);
        glViewport(0, 0, m_internalWidth, m_internalHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_resolveShader->Bind();
        BindCommonInputs(m_resolveShader, internalContext);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, m_rawIndirectRenderTarget->GetColorTextureID());
        m_resolveShader->SetUniform("uRawIndirectTexture", 5);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, previousHistoryColorTarget->GetColorTextureID());
        m_resolveShader->SetUniform("uHistoryColorTexture", 6);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, previousHistoryMetadataTarget->GetColorTextureID());
        m_resolveShader->SetUniform("uHistoryMetadataTexture", 7);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID());
        m_resolveShader->SetUniform("uSceneMotionTexture", 8);
        m_resolveShader->SetUniform("uView", context.renderContext.cameraData.view);
        m_resolveShader->SetUniform("uPreviousView", m_previousView);
        m_resolveShader->SetUniform("uPreviousViewProjection", m_previousViewProjection);
        m_resolveShader->SetUniform("uBlurRadius", m_blurRadius);
        m_resolveShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
        m_resolveShader->SetUniform("uTemporalBlend", m_temporalBlend);
        m_resolveShader->SetUniform("uHistoryDepthThreshold", m_historyDepthThreshold);
        m_resolveShader->SetUniform("uHistoryNormalThreshold", m_historyNormalThreshold);
        m_resolveShader->SetUniform("uDepthPhi", m_depthPhi);
        m_resolveShader->SetUniform("uNormalPhi", m_normalPhi);
        m_resolveShader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
        m_resolveShader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
        DrawFullscreenTriangle();

        Graphics::BindRenderTarget(resolvedHistoryMetadataTarget);
        glViewport(0, 0, m_internalWidth, m_internalHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_historyMetadataShader->Bind();
        BindCommonInputs(m_historyMetadataShader, internalContext);
        m_historyMetadataShader->SetUniform("uView", context.renderContext.cameraData.view);
        m_historyMetadataShader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
        m_historyMetadataShader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
        DrawFullscreenTriangle();

        m_historyIndex = resolvedHistoryIndex;
        m_previousView = context.renderContext.cameraData.view;
        m_previousViewProjection = viewProjection;
        m_hasHistory = true;
        return resolvedHistoryColorTarget;
    }

    void SSGIEffect::RenderComposite(const PostProcessContext &context, RenderTarget *resolvedIndirectTarget)
    {
        if (!m_compositeShader || !resolvedIndirectTarget || !context.sourceRenderTarget || !context.destinationRenderTarget)
        {
            return;
        }

        BeginApply(context);

        m_compositeShader->Bind();
        BindCommonInputs(m_compositeShader, context);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, resolvedIndirectTarget->GetColorTextureID());
        m_compositeShader->SetUniform("uResolvedIndirectTexture", 5);
        m_compositeShader->SetUniform("uOutputMode", m_outputMode);
        DrawFullscreenTriangle();

        EndApply();
    }

    void SSGIEffect::Apply(const PostProcessContext &context)
    {
        if (!context.sourceRenderTarget || !context.destinationRenderTarget)
        {
            return;
        }

        RenderTarget *resolvedIndirectTarget = GenerateResolvedIndirectLighting(context, context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight());
        if (!resolvedIndirectTarget)
        {
            return;
        }

        RenderComposite(context, resolvedIndirectTarget);
    }

    void SSGIEffect::EnsureInternalTargets(int width, int height)
    {
        const int targetWidth = std::max(1, m_halfResolution ? width / 2 : width);
        const int targetHeight = std::max(1, m_halfResolution ? height / 2 : height);
        if (targetWidth != m_internalWidth || targetHeight != m_internalHeight)
        {
            m_internalWidth = targetWidth;
            m_internalHeight = targetHeight;
            ResetHistory();
        }

        if (!m_rawIndirectRenderTarget)
        {
            m_rawIndirectRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = m_internalWidth,
                .height = m_internalHeight,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_rawIndirectRenderTarget->GetWidth() != m_internalWidth || m_rawIndirectRenderTarget->GetHeight() != m_internalHeight)
        {
            m_rawIndirectRenderTarget->Resize(m_internalWidth, m_internalHeight);
        }

        for (auto &historyRenderTarget : m_historyColorRenderTargets)
        {
            if (!historyRenderTarget)
            {
                historyRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                    .width = m_internalWidth,
                    .height = m_internalHeight,
                    .clearColor = glm::vec4(0.0f),
                });
            }
            else if (historyRenderTarget->GetWidth() != m_internalWidth || historyRenderTarget->GetHeight() != m_internalHeight)
            {
                historyRenderTarget->Resize(m_internalWidth, m_internalHeight);
            }
        }

        for (auto &historyMetadataRenderTarget : m_historyMetadataRenderTargets)
        {
            if (!historyMetadataRenderTarget)
            {
                historyMetadataRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                    .width = m_internalWidth,
                    .height = m_internalHeight,
                    .clearColor = glm::vec4(0.0f),
                });
            }
            else if (historyMetadataRenderTarget->GetWidth() != m_internalWidth || historyMetadataRenderTarget->GetHeight() != m_internalHeight)
            {
                historyMetadataRenderTarget->Resize(m_internalWidth, m_internalHeight);
            }
        }
    }

    void SSGIEffect::GenerateNoiseTexture()
    {
        if (m_noiseTexture != 0)
        {
            glDeleteTextures(1, &m_noiseTexture);
            m_noiseTexture = 0;
        }

        std::mt19937 generator(2121u);
        std::uniform_real_distribution<float> randomSigned(-1.0f, 1.0f);
        std::array<glm::vec4, kNoiseTextureSize * kNoiseTextureSize> noiseData{};

        for (glm::vec4 &noiseSample : noiseData)
        {
            glm::vec3 noiseVector(
                randomSigned(generator),
                randomSigned(generator),
                randomSigned(generator) * 0.25f);
            noiseSample = EncodeNoiseVector(glm::normalize(noiseVector));
        }

        glGenTextures(1, &m_noiseTexture);
        glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, kNoiseTextureSize, kNoiseTextureSize, 0, GL_RGBA, GL_FLOAT, noiseData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    void SSGIEffect::ResetHistory()
    {
        m_hasHistory = false;
        m_historyIndex = 0;
    }
}