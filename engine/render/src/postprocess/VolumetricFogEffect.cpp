#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kDirectionalShadowCascadeTextureStartSlot = 5;
        constexpr int kEnvironmentTextureSlot = kDirectionalShadowCascadeTextureStartSlot + scene::kMaxDirectionalShadowCascades;
        constexpr int kMinStepCount = 4;
        constexpr int kMaxStepCount = 64;

        const scene::Light *FindPrimaryDirectionalLight(const RenderContext &renderContext)
        {
            if (!renderContext.lights)
            {
                return nullptr;
            }

            const scene::Light *bestLight = nullptr;
            float bestWeight = 0.0f;

            for (const auto *light : *renderContext.lights)
            {
                if (!light || light->type != scene::LightType::Directional)
                {
                    continue;
                }

                const float luminance = 0.2126f * light->color.r + 0.7152f * light->color.g + 0.0722f * light->color.b;
                const float weight = std::max(light->intensity, 0.0f) * luminance;
                if (!bestLight || weight > bestWeight)
                {
                    bestLight = light;
                    bestWeight = weight;
                }
            }

            return bestLight;
        }

        void BindDirectionalShadowInputs(Shader *shader, const scene::Light *directionalLight)
        {
            if (!shader)
            {
                return;
            }

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                const int textureSlot = kDirectionalShadowCascadeTextureStartSlot + cascadeIndex;
                glActiveTexture(GL_TEXTURE0 + textureSlot);
                glBindTexture(GL_TEXTURE_2D, 0);
                shader->SetUniform("uShadowCascadeMap" + std::to_string(cascadeIndex), textureSlot);
            }

            int cascadeCount = 0;
            float shadowSoftness = 1.0f;
            float cascadeBlendDistance = 0.0f;

            if (directionalLight && directionalLight->castsShadows)
            {
                cascadeCount = directionalLight->activeShadowCascadeCount;
                shadowSoftness = directionalLight->directionalShadowSettings.softness;
                cascadeBlendDistance = directionalLight->directionalShadowSettings.cascadeBlendDistance;

                for (int cascadeIndex = 0; cascadeIndex < directionalLight->activeShadowCascadeCount; ++cascadeIndex)
                {
                    const auto *shadowCascadeMap = directionalLight->shadowCascadeMaps[cascadeIndex].get();
                    if (!shadowCascadeMap)
                    {
                        continue;
                    }

                    glActiveTexture(GL_TEXTURE0 + kDirectionalShadowCascadeTextureStartSlot + cascadeIndex);
                    glBindTexture(GL_TEXTURE_2D, shadowCascadeMap->GetTextureID());
                }
            }

            shader->SetUniform("uCascadeCount", cascadeCount);
            shader->SetUniform("uShadowSoftness", shadowSoftness);
            shader->SetUniform("uCascadeBlendDistance", cascadeBlendDistance);

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                const glm::mat4 lightSpaceMatrix = directionalLight ? directionalLight->shadowCascadeMatrices[cascadeIndex] : glm::mat4(1.0f);
                const float cascadeSplit = directionalLight ? directionalLight->shadowCascadeSplits[cascadeIndex] : 0.0f;
                shader->SetUniform("uCascadeLightSpaceMatrices[" + std::to_string(cascadeIndex) + "]", lightSpaceMatrix);
                shader->SetUniform("uCascadeSplits[" + std::to_string(cascadeIndex) + "]", cascadeSplit);
            }
        }

        void BindEnvironmentInput(Shader *shader, const scene::Scene *scene)
        {
            if (!shader)
            {
                return;
            }

            const auto *environmentTexture = scene ? scene->GetEnvironmentMapTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kEnvironmentTextureSlot);
            glBindTexture(GL_TEXTURE_2D, environmentTexture ? environmentTexture->GetTextureID() : 0);
            shader->SetUniform("uEnvironmentMap", kEnvironmentTextureSlot);
            shader->SetUniform("uEnvironmentEnabled", environmentTexture ? 1 : 0);
            shader->SetUniform("uEnvironmentIntensity", scene ? scene->GetEnvironmentIntensity() : 1.0f);

            float environmentMaxMipLevel = 0.0f;
            if (environmentTexture)
            {
                const int environmentDimension = std::max(environmentTexture->GetWidth(), environmentTexture->GetHeight());
                if (environmentDimension > 0)
                {
                    environmentMaxMipLevel = static_cast<float>(std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(environmentDimension))))));
                }
            }
            shader->SetUniform("uEnvironmentMaxMipLevel", environmentMaxMipLevel);
        }
    }

    std::vector<PostProcessParameter> VolumetricFogEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Density",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_density),
            },
            PostProcessParameter{
                .name = "Height Falloff",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_heightFalloff),
            },
            PostProcessParameter{
                .name = "Height Offset",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_heightOffset),
            },
            PostProcessParameter{
                .name = "Max Distance",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_maxDistance),
            },
            PostProcessParameter{
                .name = "Scattering",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_scattering),
            },
            PostProcessParameter{
                .name = "Anisotropy",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_anisotropy),
            },
            PostProcessParameter{
                .name = "Ambient Contribution",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_ambientContribution),
            },
            PostProcessParameter{
                .name = "Directional Contribution",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_directionalContribution),
            },
            PostProcessParameter{
                .name = "Max Opacity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_maxOpacity),
            },
            PostProcessParameter{
                .name = "Step Count",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_stepCount),
            },
            PostProcessParameter{
                .name = "Color R",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_fogColor.r),
            },
            PostProcessParameter{
                .name = "Color G",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_fogColor.g),
            },
            PostProcessParameter{
                .name = "Color B",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_fogColor.b),
            },
        };
    }

    void VolumetricFogEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Density")
            {
                m_density = std::max(std::stof(parameter.value), 0.0f);
            }
            else if (parameter.name == "Height Falloff")
            {
                m_heightFalloff = std::max(std::stof(parameter.value), 0.0f);
            }
            else if (parameter.name == "Height Offset")
            {
                m_heightOffset = std::stof(parameter.value);
            }
            else if (parameter.name == "Max Distance")
            {
                m_maxDistance = std::max(std::stof(parameter.value), 0.1f);
            }
            else if (parameter.name == "Scattering")
            {
                m_scattering = glm::clamp(std::stof(parameter.value), 0.0f, 2.0f);
            }
            else if (parameter.name == "Anisotropy")
            {
                m_anisotropy = glm::clamp(std::stof(parameter.value), -0.85f, 0.85f);
            }
            else if (parameter.name == "Ambient Contribution")
            {
                m_ambientContribution = glm::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Directional Contribution")
            {
                m_directionalContribution = glm::clamp(std::stof(parameter.value), 0.0f, 32.0f);
            }
            else if (parameter.name == "Max Opacity")
            {
                m_maxOpacity = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Step Count")
            {
                m_stepCount = std::clamp(std::stoi(parameter.value), kMinStepCount, kMaxStepCount);
            }
            else if (parameter.name == "Color R")
            {
                m_fogColor.r = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Color G")
            {
                m_fogColor.g = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Color B")
            {
                m_fogColor.b = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
        }
    }

    void VolumetricFogEffect::Initialize()
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
            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uShadowCascadeMap0;
            uniform sampler2D uShadowCascadeMap1;
            uniform sampler2D uShadowCascadeMap2;
            uniform sampler2D uShadowCascadeMap3;
            uniform sampler2D uEnvironmentMap;
            uniform mat4 uViewMatrix;
            uniform mat4 uInverseViewMatrix;
            uniform mat4 uInverseProjectionMatrix;
            uniform vec3 uCameraPosition;
            uniform vec3 uFogColor;
            uniform vec3 uLightDirection;
            uniform vec3 uLightColor;
            uniform float uLightIntensity;
            uniform float uFogDensity;
            uniform float uHeightFalloff;
            uniform float uHeightOffset;
            uniform float uMaxDistance;
            uniform float uScattering;
            uniform float uAnisotropy;
            uniform float uAmbientContribution;
            uniform float uDirectionalContribution;
            uniform float uMaxOpacity;
            uniform float uFrameIndex;
            uniform float uEnvironmentIntensity;
            uniform float uEnvironmentMaxMipLevel;
            uniform float uShadowSoftness;
            uniform float uCascadeBlendDistance;
            uniform int uStepCount;
            uniform int uCascadeCount;
            uniform int uEnvironmentEnabled;
            uniform int uHasDirectionalLight;
            uniform mat4 uCascadeLightSpaceMatrices[4];
            uniform float uCascadeSplits[4];

            const float PI = 3.14159265359;

            float Saturate(float value)
            {
                return clamp(value, 0.0, 1.0);
            }

            float InterleavedGradientNoise(vec2 pixel, float frameIndex)
            {
                return fract(52.9829189 * fract(dot(pixel + frameIndex, vec2(0.06711056, 0.00583715))));
            }

            vec3 GetWorldRayDirection(vec2 uv)
            {
                vec2 clip = uv * 2.0 - 1.0;
                vec4 viewDirection = uInverseProjectionMatrix * vec4(clip, 1.0, 1.0);
                vec3 rayDirectionView = normalize(viewDirection.xyz / max(viewDirection.w, 0.0001));
                return normalize((uInverseViewMatrix * vec4(rayDirectionView, 0.0)).xyz);
            }

            float ComputeDensity(vec3 worldPosition)
            {
                float heightTerm = exp(-(worldPosition.y - uHeightOffset) * uHeightFalloff);
                return max(uFogDensity * heightTerm, 0.0);
            }

            float ComputePhase(float cosTheta)
            {
                float g = clamp(uAnisotropy, -0.85, 0.85);
                float denominator = max(1.0 + g * g - 2.0 * g * cosTheta, 0.0001);
                return (1.0 - g * g) / (12.566370614359172 * pow(denominator, 1.5));
            }

            float ComputeDirectionalInscattering(float cosTheta)
            {
                float phase = ComputePhase(cosTheta);
                float forwardScatter = pow(Saturate(cosTheta), mix(8.0, 2.0, Saturate(uAnisotropy * 0.5 + 0.5)));
                return phase * 12.566370614359172 + forwardScatter * 0.75;
            }

            vec2 DirectionToLatLongUv(vec3 direction)
            {
                vec3 dir = normalize(direction);
                float longitude = atan(dir.z, dir.x);
                float latitude = asin(clamp(dir.y, -1.0, 1.0));
                return vec2(longitude / (2.0 * PI) + 0.5, latitude / PI + 0.5);
            }

            vec3 SampleEnvironment(vec3 direction, float lod)
            {
                if (uEnvironmentEnabled == 0)
                {
                    return vec3(0.0);
                }

                vec2 uv = DirectionToLatLongUv(direction);
                return max(textureLod(uEnvironmentMap, uv, clamp(lod, 0.0, uEnvironmentMaxMipLevel)).rgb, vec3(0.0)) * uEnvironmentIntensity;
            }

            vec3 ComputeFogAmbientColor(vec3 direction)
            {
                if (uEnvironmentEnabled != 0)
                {
                    float lod = min(max(uEnvironmentMaxMipLevel * 0.35, 1.0), uEnvironmentMaxMipLevel);
                    return SampleEnvironment(direction, lod);
                }

                return uFogColor;
            }

            float SampleShadowMapPCF(sampler2D shadowMap, vec3 projectedCoords)
            {
                vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
                float filterRadius = max(uShadowSoftness, 0.5);
                float shadow = 0.0;
                float totalWeight = 0.0;
                for (int y = -1; y <= 1; ++y)
                {
                    for (int x = -1; x <= 1; ++x)
                    {
                        vec2 offset = vec2(float(x), float(y)) * texelSize * filterRadius;
                        vec2 sampleCoords = projectedCoords.xy + offset;
                        float closestDepth = texture(shadowMap, sampleCoords).r;
                        float weight = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
                        shadow += projectedCoords.z > closestDepth ? weight : 0.0;
                        totalWeight += weight;
                    }
                }

                return shadow / max(totalWeight, 0.0001);
            }

            float SampleDirectionalCascadeShadow(int cascadeIndex, vec3 projectedCoords)
            {
                if (cascadeIndex == 0)
                {
                    return SampleShadowMapPCF(uShadowCascadeMap0, projectedCoords);
                }

                if (cascadeIndex == 1)
                {
                    return SampleShadowMapPCF(uShadowCascadeMap1, projectedCoords);
                }

                if (cascadeIndex == 2)
                {
                    return SampleShadowMapPCF(uShadowCascadeMap2, projectedCoords);
                }

                return SampleShadowMapPCF(uShadowCascadeMap3, projectedCoords);
            }

            int SelectDirectionalCascadeIndex(float viewDepth)
            {
                for (int cascadeIndex = 0; cascadeIndex < uCascadeCount; ++cascadeIndex)
                {
                    if (viewDepth <= uCascadeSplits[cascadeIndex])
                    {
                        return cascadeIndex;
                    }
                }

                return max(uCascadeCount - 1, 0);
            }

            float ComputeDirectionalCascadeShadow(vec3 worldPosition, int cascadeIndex)
            {
                vec4 lightSpacePosition = uCascadeLightSpaceMatrices[cascadeIndex] * vec4(worldPosition, 1.0);
                vec3 projectedCoords = lightSpacePosition.xyz / max(lightSpacePosition.w, 0.0001);
                projectedCoords = projectedCoords * 0.5 + 0.5;
                projectedCoords.z = max(projectedCoords.z - 0.00035, 0.0);

                if (projectedCoords.z < 0.0 || projectedCoords.z > 1.0 || projectedCoords.x < 0.0 || projectedCoords.x > 1.0 || projectedCoords.y < 0.0 || projectedCoords.y > 1.0)
                {
                    return 0.0;
                }

                return SampleDirectionalCascadeShadow(cascadeIndex, projectedCoords);
            }

            float ComputeDirectionalLightShadow(vec3 worldPosition)
            {
                if (uHasDirectionalLight == 0 || uCascadeCount <= 0)
                {
                    return 0.0;
                }

                float viewDepth = abs((uViewMatrix * vec4(worldPosition, 1.0)).z);
                if (viewDepth > uCascadeSplits[uCascadeCount - 1])
                {
                    return 0.0;
                }

                int cascadeIndex = SelectDirectionalCascadeIndex(viewDepth);
                float shadow = ComputeDirectionalCascadeShadow(worldPosition, cascadeIndex);

                if (cascadeIndex < uCascadeCount - 1)
                {
                    float splitDistance = uCascadeSplits[cascadeIndex];
                    float blendStart = max(splitDistance - uCascadeBlendDistance, 0.0);
                    if (viewDepth > blendStart)
                    {
                        float nextShadow = ComputeDirectionalCascadeShadow(worldPosition, cascadeIndex + 1);
                        float blendFactor = clamp((viewDepth - blendStart) / max(splitDistance - blendStart, 0.0001), 0.0, 1.0);
                        shadow = mix(shadow, nextShadow, blendFactor);
                    }
                }

                return shadow;
            }

            void main()
            {
                vec3 sceneColor = texture(uSceneTexture, UV).rgb;
                float sceneDepth = texture(uSceneDepthTexture, UV).r;
                vec3 rayDirection = GetWorldRayDirection(UV);
                vec3 surfacePosition = texture(uScenePositionTexture, UV).rgb;
                vec3 fogTint = max(uFogColor, vec3(0.0));
                vec3 ambientFogColor = ComputeFogAmbientColor(rayDirection) * fogTint;

                float hitDistance = uMaxDistance;
                if (sceneDepth < 0.9999)
                {
                    hitDistance = min(distance(surfacePosition, uCameraPosition), uMaxDistance);
                }

                if (hitDistance <= 0.0001 || uFogDensity <= 0.0 || uStepCount <= 0)
                {
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                float stepLength = hitDistance / float(uStepCount);
                float jitter = InterleavedGradientNoise(gl_FragCoord.xy, uFrameIndex);
                float currentDistance = stepLength * jitter;
                vec3 accumulatedLight = vec3(0.0);
                float transmittance = 1.0;
                vec3 viewDirection = -rayDirection;
                float directionalInscattering = uHasDirectionalLight != 0
                    ? ComputeDirectionalInscattering(dot(viewDirection, normalize(uLightDirection)))
                    : 0.0;

                for (int stepIndex = 0; stepIndex < uStepCount; ++stepIndex)
                {
                    if (currentDistance >= hitDistance)
                    {
                        break;
                    }

                    float segmentLength = min(stepLength, hitDistance - currentDistance);
                    vec3 samplePosition = uCameraPosition + rayDirection * (currentDistance + 0.5 * segmentLength);
                    float density = ComputeDensity(samplePosition);
                    float extinction = max(density * segmentLength, 0.0);
                    float segmentTransmittance = exp(-extinction);
                    float segmentFog = 1.0 - segmentTransmittance;
                    float shadow = ComputeDirectionalLightShadow(samplePosition);
                    float lightVisibility = 1.0 - shadow;
                    float localDensityBoost = 1.0 + density * 24.0;
                    float multipleScattering = 1.0 - exp(-density * segmentLength * 6.0);
                    vec3 ambientScatter = ambientFogColor * uAmbientContribution * (0.25 + 0.75 * multipleScattering);
                    vec3 directionalScatter = uHasDirectionalLight != 0
                        ? (uLightColor * uLightIntensity * directionalInscattering * uScattering * uDirectionalContribution * lightVisibility * localDensityBoost)
                        : vec3(0.0);
                    vec3 fogLighting = ambientScatter + (fogTint * directionalScatter);

                    accumulatedLight += transmittance * segmentFog * fogLighting;
                    transmittance *= segmentTransmittance;

                    if (transmittance <= 0.001)
                    {
                        transmittance = 0.001;
                        break;
                    }

                    currentDistance += stepLength;
                }

                float totalFog = Saturate(1.0 - transmittance);
                if (totalFog <= 0.0001)
                {
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                vec3 fogRadiance = accumulatedLight / max(totalFog, 0.0001);
                float fogFactor = min(totalFog, uMaxOpacity);
                vec3 finalColor = mix(sceneColor, fogRadiance, fogFactor);
                FragColor = vec4(finalColor, 1.0);
            }
        )";

        m_shader = Shader::Create(source);
    }

    void VolumetricFogEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget || !context.renderContext.hasCameraData)
        {
            return;
        }

        BeginApply(context);

        const glm::mat4 inverseView = glm::inverse(context.renderContext.cameraData.view);
        const glm::mat4 inverseProjection = glm::inverse(context.renderContext.cameraData.projection);
        const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
        const scene::Light *primaryDirectionalLight = FindPrimaryDirectionalLight(context.renderContext);

        glm::vec3 lightDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 lightColor = glm::vec3(1.0f);
        float lightIntensity = 0.0f;
        int hasDirectionalLight = 0;

        if (primaryDirectionalLight)
        {
            lightDirection = glm::normalize(-primaryDirectionalLight->direction);
            lightColor = glm::max(primaryDirectionalLight->color, glm::vec3(0.0f));
            lightIntensity = glm::max(primaryDirectionalLight->intensity, 0.0f);
            hasDirectionalLight = 1;
        }

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        BindDirectionalShadowInputs(m_shader, primaryDirectionalLight);
        BindEnvironmentInput(m_shader, context.renderContext.scene);
        m_shader->SetUniform("uViewMatrix", context.renderContext.cameraData.view);
        m_shader->SetUniform("uInverseViewMatrix", inverseView);
        m_shader->SetUniform("uInverseProjectionMatrix", inverseProjection);
        m_shader->SetUniform("uCameraPosition", cameraPosition);
        m_shader->SetUniform("uFogColor", m_fogColor);
        m_shader->SetUniform("uLightDirection", lightDirection);
        m_shader->SetUniform("uLightColor", lightColor);
        m_shader->SetUniform("uLightIntensity", lightIntensity);
        m_shader->SetUniform("uFogDensity", std::max(m_density, 0.0f));
        m_shader->SetUniform("uHeightFalloff", std::max(m_heightFalloff, 0.0f));
        m_shader->SetUniform("uHeightOffset", m_heightOffset);
        m_shader->SetUniform("uMaxDistance", std::max(m_maxDistance, 0.1f));
        m_shader->SetUniform("uScattering", m_scattering);
        m_shader->SetUniform("uAnisotropy", m_anisotropy);
        m_shader->SetUniform("uAmbientContribution", m_ambientContribution);
        m_shader->SetUniform("uDirectionalContribution", m_directionalContribution);
        m_shader->SetUniform("uMaxOpacity", m_maxOpacity);
        m_shader->SetUniform("uFrameIndex", static_cast<float>(context.renderContext.frameSequence & 1023u));
        m_shader->SetUniform("uStepCount", std::clamp(m_stepCount, kMinStepCount, kMaxStepCount));
        m_shader->SetUniform("uHasDirectionalLight", hasDirectionalLight);
        DrawFullscreenTriangle();

        EndApply();
    }
}