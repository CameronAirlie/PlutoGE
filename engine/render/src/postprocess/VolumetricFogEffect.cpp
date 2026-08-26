#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/UniformNames.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kDirectionalShadowCascadeTextureStartSlot = 5;
        constexpr int kOceanStateTextureSlot = 9;
        constexpr int kEnvironmentTextureSlot = 10;
        constexpr int kFogAmbientTextureSlot = 11;
        constexpr int kMinStepCount = 16;
        constexpr int kMaxStepCount = 64;
        constexpr int kMinShadowStepStride = 1;
        constexpr int kMaxShadowStepStride = 4;

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }

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

            static const auto shadowMapNames =
                MakeNumberedUniformNames<scene::kMaxDirectionalShadowCascades>("uShadowCascadeMap");
            static const auto cascadeMatrixNames =
                MakeArrayUniformNames<scene::kMaxDirectionalShadowCascades>("uCascadeLightSpaceMatrices");
            static const auto cascadeSplitNames =
                MakeArrayUniformNames<scene::kMaxDirectionalShadowCascades>("uCascadeSplits");
            static const auto cascadeOriginNames =
                MakeArrayUniformNames<scene::kMaxDirectionalShadowCascades>("uCascadeWorldOrigins");

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                const int textureSlot = kDirectionalShadowCascadeTextureStartSlot + cascadeIndex;
                Graphics::ActiveTexture(GL_TEXTURE0 + textureSlot);
                Graphics::BindTexture(GL_TEXTURE_2D, 0);
                shader->SetUniform(shadowMapNames[cascadeIndex], textureSlot);
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

                    Graphics::ActiveTexture(GL_TEXTURE0 + kDirectionalShadowCascadeTextureStartSlot + cascadeIndex);
                    Graphics::BindTexture(GL_TEXTURE_2D, shadowCascadeMap->GetTextureID());
                }
            }

            shader->SetUniform("uCascadeCount", cascadeCount);
            shader->SetUniform("uShadowSoftness", shadowSoftness);
            shader->SetUniform("uCascadeBlendDistance", cascadeBlendDistance);

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                const glm::mat4 lightSpaceMatrix = directionalLight ? directionalLight->shadowCascadeMatrices[cascadeIndex] : glm::mat4(1.0f);
                const float cascadeSplit = directionalLight ? directionalLight->shadowCascadeSplits[cascadeIndex] : 0.0f;
                shader->SetUniform(cascadeMatrixNames[cascadeIndex], lightSpaceMatrix);
                shader->SetUniform(cascadeSplitNames[cascadeIndex], cascadeSplit);
            }
            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                const glm::vec3 shadowWorldOrigin = directionalLight ? directionalLight->shadowCascadeWorldOrigins[cascadeIndex] : glm::vec3(0.0f);
                shader->SetUniform(cascadeOriginNames[cascadeIndex], shadowWorldOrigin);
            }
        }
    }

    VolumetricFogEffect::~VolumetricFogEffect()
    {
        if (m_shadowCompareSampler)
            glDeleteSamplers(1, &m_shadowCompareSampler);
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
                .name = "Shadow Step Stride",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_shadowStepStride),
            },
            PostProcessParameter{
                .name = "Half Resolution",
                .type = PostProcessParameterType::Bool,
                .value = m_halfResolution ? "true" : "false",
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
        m_hasHistory = false;
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
            else if (parameter.name == "Shadow Step Stride")
            {
                m_shadowStepStride = std::clamp(std::stoi(parameter.value), kMinShadowStepStride, kMaxShadowStepStride);
            }
            else if (parameter.name == "Half Resolution")
            {
                const bool nextHalfResolution = ParseBool(parameter.value);
                if (m_halfResolution != nextHalfResolution)
                {
                    m_halfResolution = nextHalfResolution;
                    m_internalWidth = 0;
                    m_internalHeight = 0;
                }
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

            uniform sampler2D uSceneDepthTexture;
            uniform sampler2D uOceanStateTexture;
            uniform sampler2D uEnvironmentMap;
            uniform sampler2D uFogAmbientTexture;
            uniform sampler2DShadow uShadowCascadeMap0;
            uniform sampler2DShadow uShadowCascadeMap1;
            uniform sampler2DShadow uShadowCascadeMap2;
            uniform sampler2DShadow uShadowCascadeMap3;
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
            uniform float uShadowSoftness;
            uniform float uCascadeBlendDistance;
            uniform int uStepCount;
            uniform int uShadowStepStride;
            uniform int uCascadeCount;
            uniform int uHasDirectionalLight;
            uniform int uEnvironmentEnabled;
            uniform float uEnvironmentIntensity;
            uniform int uTemporalSampling;
            uniform float uFrameIndex;
            uniform vec3 uCascadeWorldOrigins[4];
            uniform mat4 uCascadeLightSpaceMatrices[4];
            uniform float uCascadeSplits[4];

            float Saturate(float value)
            {
                return clamp(value, 0.0, 1.0);
            }

            float PixelJitter(vec2 pixel, float frame)
            {
                // Interleaved gradient noise has little low-frequency energy,
                // so discrete shadow samples become fine noise instead of
                // coherent copies of each caster along the view ray.
                float noise = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
                return fract(noise + frame * 0.61803398875);
            }

            vec3 GetWorldRayDirection(vec2 uv)
            {
                vec2 clip = uv * 2.0 - 1.0;
                vec4 viewDirection = uInverseProjectionMatrix * vec4(clip, 1.0, 1.0);
                vec3 rayDirectionView = normalize(viewDirection.xyz / max(viewDirection.w, 0.0001));
                return normalize((uInverseViewMatrix * vec4(rayDirectionView, 0.0)).xyz);
            }

            vec3 ReconstructWorldPosition(vec2 uv, float depth)
            {
                vec4 clipPosition = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                vec4 viewPosition = uInverseProjectionMatrix * clipPosition;
                viewPosition /= max(viewPosition.w, 0.0001);
                return (uInverseViewMatrix * vec4(viewPosition.xyz, 1.0)).xyz;
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
                // Keep the phase function energy conserving.  The previous
                // path multiplied Henyey-Greenstein by 4 PI and then added a
                // second forward lobe, which created radiance and made long
                // horizontal paths blow out around a low sun.
                return ComputePhase(cosTheta);
            }

            float SampleShadowMapPCF(sampler2DShadow shadowMap, vec3 projectedCoords)
            {
                float receiverDepth = projectedCoords.z - 0.00045;
                // A linear comparison sampler performs the same four depth
                // comparisons and bilinear PCF blend in dedicated texture
                // hardware instead of issuing four explicit shader fetches.
                return 1.0 - texture(shadowMap, vec3(projectedCoords.xy, receiverDepth));
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

            int SelectDirectionalCascadeIndex(float cameraDistance)
            {
                for (int cascadeIndex = 0; cascadeIndex < uCascadeCount; ++cascadeIndex)
                {
                    if (cameraDistance <= uCascadeSplits[cascadeIndex])
                    {
                        return cascadeIndex;
                    }
                }

                return max(uCascadeCount - 1, 0);
            }

            float ComputeDirectionalCascadeShadow(vec3 worldPosition, int cascadeIndex, out bool hasCoverage)
            {
                vec4 lightSpacePosition = uCascadeLightSpaceMatrices[cascadeIndex] * vec4(worldPosition - uCascadeWorldOrigins[cascadeIndex], 1.0);
                vec3 projectedCoords = lightSpacePosition.xyz / max(lightSpacePosition.w, 0.0001);
                projectedCoords = projectedCoords * 0.5 + 0.5;

                hasCoverage = !(projectedCoords.z < 0.0 || projectedCoords.z > 1.0 ||
                                projectedCoords.x < 0.0 || projectedCoords.x > 1.0 ||
                                projectedCoords.y < 0.0 || projectedCoords.y > 1.0);
                if (!hasCoverage)
                {
                    return 0.0;
                }

                return SampleDirectionalCascadeShadow(cascadeIndex, projectedCoords);
            }

            float ComputeDirectionalLightShadow(vec3 worldPosition, float viewDepth)
            {
                if (uHasDirectionalLight == 0 || uCascadeCount <= 0)
                {
                    return 0.0;
                }

                if (viewDepth > uCascadeSplits[uCascadeCount - 1])
                {
                    // There is no occlusion information beyond the final
                    // cascade. Treating that region as fully visible creates
                    // a bright ring of sun-lit fog at the shadow horizon.
                    return 1.0;
                }

                int cascadeIndex = SelectDirectionalCascadeIndex(viewDepth);
                bool hasCascadeCoverage = false;
                float shadow = ComputeDirectionalCascadeShadow(worldPosition, cascadeIndex, hasCascadeCoverage);
                if (!hasCascadeCoverage)
                {
                    return 1.0;
                }

                if (cascadeIndex < uCascadeCount - 1)
                {
                    float splitDistance = uCascadeSplits[cascadeIndex];
                    float blendStart = max(splitDistance - uCascadeBlendDistance, 0.0);
                    if (viewDepth > blendStart)
                    {
                        bool hasNextCascadeCoverage = false;
                        float nextShadow = ComputeDirectionalCascadeShadow(worldPosition, cascadeIndex + 1, hasNextCascadeCoverage);
                        if (hasNextCascadeCoverage)
                        {
                            float blendFactor = clamp((viewDepth - blendStart) / max(splitDistance - blendStart, 0.0001), 0.0, 1.0);
                            shadow = mix(shadow, nextShadow, blendFactor);
                        }
                    }
                }

                // Fade visibility into the end of the final shadow cascade.
                // This avoids a hard transition to the conservative
                // no-coverage result while preventing distant fog from being
                // assumed to receive unobstructed sunlight.
                if (cascadeIndex == uCascadeCount - 1)
                {
                    float splitDistance = uCascadeSplits[cascadeIndex];
                    float fadeDistance = max(uCascadeBlendDistance, splitDistance * 0.05);
                    float fadeStart = max(splitDistance - fadeDistance, 0.0);
                    float coverageFade = clamp((viewDepth - fadeStart) / max(fadeDistance, 0.0001), 0.0, 1.0);
                    shadow = mix(shadow, 1.0, coverageFade);
                }

                return shadow;
            }

            void main()
            {
                if (texture(uOceanStateTexture, vec2(0.5)).r > 0.5)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                float sceneDepth = texture(uSceneDepthTexture, UV).r;
                bool isSky = sceneDepth <= 0.000001;
                vec3 rayDirection = GetWorldRayDirection(UV);
                vec3 fogTint = max(uFogColor, vec3(0.0));
                vec3 ambientFogColor = texture(uFogAmbientTexture, vec2(0.5)).rgb;

                float hitDistance = uMaxDistance;
                // PlutoGE uses reversed-Z: cleared sky is zero and geometry is
                // greater than zero. Reconstructing a zero-depth sky sample as
                // a surface collapses its march distance and leaves it unfogged.
                if (sceneDepth > 0.000001)
                {
                    vec3 surfacePosition = ReconstructWorldPosition(UV, sceneDepth);
                    hitDistance = min(distance(surfacePosition, uCameraPosition), uMaxDistance);
                }

                if (hitDistance <= 0.0001 || uFogDensity <= 0.0 || uStepCount <= 0)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                float stepLength = hitDistance / float(uStepCount);
                // A regular midpoint march stamps the same shadow silhouette
                // at every depth plane. Use this as the seed for a stratified
                // low-discrepancy sequence below instead of shifting the whole
                // regular lattice by one shared offset.
                float frameIndex = uTemporalSampling != 0 ? uFrameIndex : 0.0;
                float rayJitter = PixelJitter(gl_FragCoord.xy, frameIndex);
                vec3 cameraForward = -normalize(vec3(uViewMatrix[0][2], uViewMatrix[1][2], uViewMatrix[2][2]));
                float viewDepthScale = max(dot(rayDirection, cameraForward), 0.0);
                vec3 accumulatedLight = vec3(0.0);
                float transmittance = 1.0;
                float directionalInscattering = uHasDirectionalLight != 0
                    ? ComputeDirectionalInscattering(dot(rayDirection, normalize(uLightDirection)))
                    : 0.0;
                float lightVisibility = 1.0;

                for (int stepIndex = 0; stepIndex < uStepCount; ++stepIndex)
                {
                    // One sample remains inside each ordered march cell, but
                    // its fractional position follows a golden-ratio sequence.
                    // Pillar silhouettes therefore cannot recur at a constant
                    // world-space interval. This costs no additional density
                    // or shadow samples and TAA integrates the changing pattern.
                    float cellJitter = fract(rayJitter + float(stepIndex) * 0.61803398875);
                    // Extinction controls the stable body of the fog, so keep
                    // it at the cell midpoint. Only decorrelate the shadow
                    // query that causes visible banding.
                    float sampleDistance = (float(stepIndex) + 0.5) * stepLength;
                    vec3 samplePosition = uCameraPosition + rayDirection * sampleDistance;
                    float sampleViewDepth = viewDepthScale * sampleDistance;
                    float density = ComputeDensity(samplePosition);
                    float extinction = max(density * stepLength, 0.0);
                    float segmentTransmittance = exp(-extinction);
                    float segmentFog = 1.0 - segmentTransmittance;
                    // Shadow lookups dominate this ray march. Adjacent samples
                    // cover a very small world-space interval at the default
                    // step count, so reuse visibility for a configurable number
                    // of integration steps. A stride of one retains the former
                    // maximum-quality path.
                    if (uHasDirectionalLight != 0 && (stepIndex % uShadowStepStride) == 0)
                    {
                        float shadowDistance = (float(stepIndex) + cellJitter) * stepLength;
                        vec3 shadowPosition = uCameraPosition + rayDirection * shadowDistance;
                        lightVisibility = 1.0 - ComputeDirectionalLightShadow(shadowPosition, viewDepthScale * shadowDistance);
                    }
                    // Environment radiance is already the incident ambient
                    // light. Do not attenuate it a second time based on the
                    // current segment thickness; segmentFog below supplies the
                    // physically relevant scattering weight.
                    vec3 ambientScatter = ambientFogColor * uAmbientContribution;
                    vec3 directionalScatter = uHasDirectionalLight != 0
                        ? (uLightColor * uLightIntensity * directionalInscattering * uScattering * uDirectionalContribution * lightVisibility)
                        : vec3(0.0);
                    vec3 fogLighting = ambientScatter + (fogTint * directionalScatter);

                    accumulatedLight += transmittance * segmentFog * fogLighting;
                    transmittance *= segmentTransmittance;

                    if (transmittance <= 0.001)
                    {
                        // Below this threshold the remaining source radiance is
                        // negligible. Resolve it to fully opaque so very bright
                        // HDR backgrounds cannot leak through the early exit.
                        transmittance = 0.0;
                        break;
                    }
                }

                float totalFog = Saturate(1.0 - transmittance);
                if (totalFog <= 0.0001)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                vec3 fogRadiance = accumulatedLight / max(totalFog, 0.0001);
                // Max Opacity is an artistic cap for finite scene surfaces.
                // Applying it to HDR sky pixels guarantees a persistent clear
                // sky contribution which tone mapping and bloom amplify. Sky
                // must retain the march's actual transmittance instead.
                float fogFactor = isSky ? totalFog : min(totalFog, uMaxOpacity);
                FragColor = vec4(fogRadiance, fogFactor);
            }
        )";

        m_shader = Shader::Create(source);

        ShaderSource ambientSource;
        ambientSource.vertexSource = source.vertexSource;
        ambientSource.fragmentSource = R"(
            #version 330 core
            out vec4 FragColor;
            uniform sampler2D uEnvironmentMap;
            uniform vec3 uFogColor;
            uniform float uEnvironmentIntensity;
            uniform int uEnvironmentEnabled;

            vec2 DirectionToEquirectangularUv(vec3 direction)
            {
                const float invPi = 0.31830988618;
                const float invTwoPi = 0.15915494309;
                vec3 normalizedDirection = normalize(direction);
                return vec2(atan(normalizedDirection.z, normalizedDirection.x) * invTwoPi + 0.5,
                            acos(clamp(normalizedDirection.y, -1.0, 1.0)) * invPi);
            }

            vec3 SampleEnvironment(vec3 direction)
            {
                return max(texture(uEnvironmentMap, DirectionToEquirectangularUv(direction)).rgb, vec3(0.0));
            }

            void main()
            {
                if (uEnvironmentEnabled == 0)
                {
                    FragColor = vec4(0.0);
                    return;
                }
                vec3 environmentRadiance =
                    SampleEnvironment(vec3( 1.0,  0.0,  0.0)) +
                    SampleEnvironment(vec3(-1.0,  0.0,  0.0)) +
                    SampleEnvironment(vec3( 0.0,  1.0,  0.0)) +
                    SampleEnvironment(vec3( 0.0, -1.0,  0.0)) +
                    SampleEnvironment(vec3( 0.0,  0.0,  1.0)) +
                    SampleEnvironment(vec3( 0.0,  0.0, -1.0)) +
                    SampleEnvironment(vec3( 1.0,  1.0,  1.0)) +
                    SampleEnvironment(vec3(-1.0,  1.0,  1.0)) +
                    SampleEnvironment(vec3( 1.0,  1.0, -1.0)) +
                    SampleEnvironment(vec3(-1.0,  1.0, -1.0)) +
                    SampleEnvironment(vec3( 1.0, -1.0,  1.0)) +
                    SampleEnvironment(vec3(-1.0, -1.0,  1.0)) +
                    SampleEnvironment(vec3( 1.0, -1.0, -1.0)) +
                    SampleEnvironment(vec3(-1.0, -1.0, -1.0));
                FragColor = vec4(max(uFogColor, vec3(0.0)) * environmentRadiance *
                                 (uEnvironmentIntensity / 14.0), 1.0);
            }
        )";
        m_ambientShader = Shader::Create(ambientSource);
        m_ambientRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
            .width = 1,
            .height = 1,
            .clearColor = glm::vec4(0.0f),
        });

        glGenSamplers(1, &m_shadowCompareSampler);
        glSamplerParameteri(m_shadowCompareSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowCompareSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowCompareSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_shadowCompareSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_shadowCompareSampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_shadowCompareSampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        const float shadowBorder[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glSamplerParameterfv(m_shadowCompareSampler, GL_TEXTURE_BORDER_COLOR, shadowBorder);

        ShaderSource temporalSource;
        temporalSource.vertexSource = source.vertexSource;
        temporalSource.fragmentSource = R"(
            #version 330 core
            in vec2 UV;
            out vec4 FragColor;
            uniform sampler2D uCurrentFog;
            uniform sampler2D uCurrentDepth;
            uniform sampler2D uHistoryFog;
            uniform sampler2D uHistoryDepth;
            uniform mat4 uInverseViewProjection;
            uniform mat4 uPreviousViewProjection;
            uniform int uHasHistory;

            void main()
            {
                vec4 current = texture(uCurrentFog, UV);
                if (uHasHistory == 0) { FragColor = current; return; }

                float depth = texture(uCurrentDepth, UV).r;
                vec4 clip = vec4(UV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                vec4 world = uInverseViewProjection * clip;
                world /= max(abs(world.w), 0.0001);
                vec4 previousClip = uPreviousViewProjection * vec4(world.xyz, 1.0);
                if (previousClip.w <= 0.0) { FragColor = current; return; }
                vec3 previousNdc = previousClip.xyz / previousClip.w;
                vec2 historyUv = previousNdc.xy * 0.5 + 0.5;
                if (any(lessThan(historyUv, vec2(0.0))) || any(greaterThan(historyUv, vec2(1.0))))
                { FragColor = current; return; }

                float historyDepth = texture(uHistoryDepth, historyUv).r;
                float expectedDepth = previousNdc.z * 0.5 + 0.5;
                bool currentSky = depth <= 0.000001;
                bool historySky = historyDepth <= 0.000001;
                float depthScale = max(max(abs(expectedDepth), abs(historyDepth)) * 0.02, 0.00005);
                float depthValidity = currentSky
                    ? (historySky ? 1.0 : 0.0)
                    : (historySky ? 0.0 : 1.0 - smoothstep(depthScale, depthScale * 3.0, abs(expectedDepth - historyDepth)));

                vec2 texel = 1.0 / vec2(textureSize(uCurrentFog, 0));
                vec4 minimumValue = current;
                vec4 maximumValue = current;
                for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                {
                    vec4 sampleValue = texture(uCurrentFog, clamp(UV + vec2(x, y) * texel, vec2(0.0), vec2(1.0)));
                    minimumValue = min(minimumValue, sampleValue);
                    maximumValue = max(maximumValue, sampleValue);
                }
                vec4 history = texture(uHistoryFog, historyUv);
                // A deliberately relaxed envelope preserves stochastic bright
                // shaft samples so they converge instead of being rejected by
                // the much tighter surface-TAA neighborhood clamp.
                vec4 extent = max(maximumValue - minimumValue, vec4(0.002));
                history = clamp(history, minimumValue - extent * 0.5, maximumValue + extent * 0.5);
                float motionPixels = length((historyUv - UV) / texel);
                float historyWeight = 0.92 * depthValidity * (1.0 - smoothstep(8.0, 48.0, motionPixels));
                FragColor = mix(current, history, historyWeight);
            }
        )";
        m_temporalShader = Shader::Create(temporalSource);

        ShaderSource compositeSource;
        compositeSource.vertexSource = source.vertexSource;
        compositeSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uFogTexture;
            uniform sampler2D uSceneDepthTexture;
            uniform vec2 uFogTexelSize;

            vec4 SampleFogBilateral(vec2 uv)
            {
                float centerDepth = texture(uSceneDepthTexture, uv).r;
                vec4 result = vec4(0.0);
                float totalWeight = 0.0;
                const vec2 offsets[5] = vec2[](vec2(0.0), vec2(1.0,0.0), vec2(-1.0,0.0), vec2(0.0,1.0), vec2(0.0,-1.0));
                for (int i = 0; i < 5; ++i)
                {
                    vec2 sampleUv = clamp(uv + offsets[i] * uFogTexelSize, vec2(0.0), vec2(1.0));
                    float sampleDepth = texture(uSceneDepthTexture, sampleUv).r;
                    bool sameClass = (centerDepth <= 0.000001) == (sampleDepth <= 0.000001);
                    float scale = max(max(centerDepth, sampleDepth) * 0.03, 0.00005);
                    float depthWeight = sameClass ? exp(-abs(centerDepth - sampleDepth) / scale) : 0.0;
                    float kernelWeight = i == 0 ? 4.0 : 1.0;
                    float weight = kernelWeight * depthWeight;
                    result += texture(uFogTexture, sampleUv) * weight;
                    totalWeight += weight;
                }
                return result / max(totalWeight, 0.0001);
            }

            void main()
            {
                vec3 sceneColor = texture(uSceneTexture, UV).rgb;
                vec4 fog = SampleFogBilateral(UV);
                float fogFactor = clamp(fog.a, 0.0, 1.0);
                vec3 finalColor = mix(sceneColor, fog.rgb, fogFactor);
                FragColor = vec4(finalColor, 1.0);
            }
        )";

        m_compositeShader = Shader::Create(compositeSource);
    }

    void VolumetricFogEffect::EnsureInternalTarget(int width, int height)
    {
        const int targetWidth = std::max(1, m_halfResolution ? width / 2 : width);
        const int targetHeight = std::max(1, m_halfResolution ? height / 2 : height);
        if (targetWidth != m_internalWidth || targetHeight != m_internalHeight)
        {
            m_internalWidth = targetWidth;
            m_internalHeight = targetHeight;
        }

        if (!m_fogRenderTarget)
        {
            m_fogRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = m_internalWidth,
                .height = m_internalHeight,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_fogRenderTarget->GetWidth() != m_internalWidth || m_fogRenderTarget->GetHeight() != m_internalHeight)
        {
            m_fogRenderTarget->Resize(m_internalWidth, m_internalHeight);
            m_hasHistory = false;
        }

        for (auto &historyTarget : m_historyTargets)
        {
            if (!historyTarget)
            {
                historyTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                    .width = m_internalWidth,
                    .height = m_internalHeight,
                    .clearColor = glm::vec4(0.0f),
                });
                m_hasHistory = false;
            }
            else if (historyTarget->GetWidth() != m_internalWidth || historyTarget->GetHeight() != m_internalHeight)
            {
                historyTarget->Resize(m_internalWidth, m_internalHeight);
                m_hasHistory = false;
            }
        }
    }

    void VolumetricFogEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !m_ambientShader || !m_temporalShader || !m_compositeShader ||
            !m_ambientRenderTarget || !m_ambientRenderTarget->IsInitialized() ||
            !context.sourceRenderTarget || !context.destinationRenderTarget || !context.renderContext.hasCameraData)
        {
            return;
        }

        EnsureInternalTarget(context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight());
        if (!m_fogRenderTarget || !m_fogRenderTarget->IsInitialized())
        {
            return;
        }

        if (m_lastHistoryFrame == 0 || context.renderContext.frameSequence != m_lastHistoryFrame + 1)
        {
            m_hasHistory = false;
        }
        m_lastHistoryFrame = context.renderContext.frameSequence;

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

        const PostProcessContext internalContext{
            .renderContext = context.renderContext,
            .sourceRenderTarget = context.sourceRenderTarget,
            .destinationRenderTarget = nullptr,
        };

        Graphics::BindRenderTarget(m_fogRenderTarget.get());
        Graphics::SetViewport(0, 0, m_internalWidth, m_internalHeight);
        Graphics::Disable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        Graphics::Disable(GL_CULL_FACE);
        Graphics::Disable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_shader->Bind();
        BindCommonInputs(m_shader, internalContext);
        const GLuint oceanSurfaceDepthTexture = context.renderContext.oceanSurfaceDepthRenderTarget
                                                     ? context.renderContext.oceanSurfaceDepthRenderTarget->GetDepthTextureID()
                                                     : 0;
        if (oceanSurfaceDepthTexture != 0)
        {
            Graphics::ActiveTexture(GL_TEXTURE1);
            Graphics::BindTexture(GL_TEXTURE_2D, oceanSurfaceDepthTexture);
            m_shader->SetUniform("uSceneDepthTexture", 1);

            // Shadow cascades occupy slots 5-8. Keep ocean state outside that
            // range or the third cascade replaces it and makes fog depend on
            // the sampled shadow map (most visibly around the horizon).
            Graphics::ActiveTexture(GL_TEXTURE0 + kOceanStateTextureSlot);
            Graphics::BindTexture(GL_TEXTURE_2D, context.renderContext.oceanSurfaceDepthRenderTarget->GetColorTextureID());
            m_shader->SetUniform("uOceanStateTexture", kOceanStateTextureSlot);
        }
        BindDirectionalShadowInputs(m_shader, primaryDirectionalLight);
        const GLuint physicalSkyTexture = context.renderContext.renderer
                                              ? context.renderContext.renderer->GetPhysicalSkyEnvironmentTextureID()
                                              : 0;
        const auto *sceneEnvironmentTexture = context.renderContext.scene
                                                  ? context.renderContext.scene->GetEnvironmentMapTexture()
                                                  : nullptr;
        const GLuint environmentTexture = physicalSkyTexture != 0
                                              ? physicalSkyTexture
                                              : (sceneEnvironmentTexture ? sceneEnvironmentTexture->GetTextureID() : 0);
        const float environmentIntensity = context.renderContext.scene
                                               ? std::max(context.renderContext.scene->GetEnvironmentIntensity(), 0.0f)
                                               : 1.0f;

        // Ambient environment lighting is constant across the fog buffer.
        // Evaluate the original 14-direction estimate once instead of once per
        // fog pixel, then broadcast it through this one-texel target.
        Graphics::BindRenderTarget(m_ambientRenderTarget.get());
        Graphics::SetViewport(0, 0, 1, 1);
        Graphics::Disable(GL_BLEND);
        m_ambientShader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0 + kEnvironmentTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, environmentTexture);
        m_ambientShader->SetUniform("uEnvironmentMap", kEnvironmentTextureSlot);
        m_ambientShader->SetUniform("uEnvironmentEnabled", environmentTexture != 0 ? 1 : 0);
        m_ambientShader->SetUniform("uEnvironmentIntensity", environmentIntensity);
        m_ambientShader->SetUniform("uFogColor", m_fogColor);
        const bool ambientTiming = context.renderContext.renderer &&
                                   context.renderContext.renderer->BeginGpuDetailTiming("Volumetric Fog / Ambient prepass");
        DrawFullscreenTriangle();
        if (ambientTiming)
            context.renderContext.renderer->EndGpuDetailTiming();

        Graphics::BindRenderTarget(m_fogRenderTarget.get());
        Graphics::SetViewport(0, 0, m_internalWidth, m_internalHeight);
        m_shader->Bind();
        BindCommonInputs(m_shader, internalContext);
        Graphics::ActiveTexture(GL_TEXTURE0 + kEnvironmentTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, environmentTexture);
        m_shader->SetUniform("uEnvironmentMap", kEnvironmentTextureSlot);
        m_shader->SetUniform("uEnvironmentEnabled", environmentTexture != 0 ? 1 : 0);
        m_shader->SetUniform("uEnvironmentIntensity", environmentIntensity);
        Graphics::ActiveTexture(GL_TEXTURE0 + kFogAmbientTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, m_ambientRenderTarget->GetColorTextureID());
        m_shader->SetUniform("uFogAmbientTexture", kFogAmbientTextureSlot);
        m_shader->SetUniform("uTemporalSampling", 1);
        m_shader->SetUniform("uFrameIndex", static_cast<float>(context.renderContext.frameSequence % 16ull));
        for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            glBindSampler(kDirectionalShadowCascadeTextureStartSlot + cascadeIndex, m_shadowCompareSampler);
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
        m_shader->SetUniform("uStepCount", std::clamp(m_stepCount, kMinStepCount, kMaxStepCount));
        m_shader->SetUniform("uShadowStepStride", std::clamp(m_shadowStepStride, kMinShadowStepStride, kMaxShadowStepStride));
        m_shader->SetUniform("uHasDirectionalLight", hasDirectionalLight);
        const bool rayMarchTiming = context.renderContext.renderer &&
                                    context.renderContext.renderer->BeginGpuDetailTiming("Volumetric Fog / Ray march");
        DrawFullscreenTriangle();
        if (rayMarchTiming)
            context.renderContext.renderer->EndGpuDetailTiming();

        for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            glBindSampler(kDirectionalShadowCascadeTextureStartSlot + cascadeIndex, 0);

        RenderTarget *historyRead = m_historyTargets[m_historyIndex].get();
        RenderTarget *historyWrite = m_historyTargets[1 - m_historyIndex].get();
        const RenderTarget *depthSource = context.renderContext.oceanSurfaceDepthRenderTarget
                                              ? context.renderContext.oceanSurfaceDepthRenderTarget
                                              : context.sourceRenderTarget;
        const glm::mat4 currentViewProjection = context.renderContext.cameraData.projection * context.renderContext.cameraData.view;
        Graphics::BindRenderTarget(historyWrite);
        Graphics::SetViewport(0, 0, m_internalWidth, m_internalHeight);
        Graphics::Disable(GL_BLEND);
        m_temporalShader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0);
        Graphics::BindTexture(GL_TEXTURE_2D, m_fogRenderTarget->GetColorTextureID());
        m_temporalShader->SetUniform("uCurrentFog", 0);
        Graphics::ActiveTexture(GL_TEXTURE1);
        Graphics::BindTexture(GL_TEXTURE_2D, depthSource->GetDepthTextureID());
        m_temporalShader->SetUniform("uCurrentDepth", 1);
        Graphics::ActiveTexture(GL_TEXTURE2);
        Graphics::BindTexture(GL_TEXTURE_2D, historyRead->GetColorTextureID());
        m_temporalShader->SetUniform("uHistoryFog", 2);
        Graphics::ActiveTexture(GL_TEXTURE3);
        Graphics::BindTexture(GL_TEXTURE_2D, historyRead->GetDepthTextureID());
        m_temporalShader->SetUniform("uHistoryDepth", 3);
        m_temporalShader->SetUniform("uInverseViewProjection", glm::inverse(currentViewProjection));
        m_temporalShader->SetUniform("uPreviousViewProjection", m_previousViewProjection);
        m_temporalShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
        const bool temporalTiming = context.renderContext.renderer &&
                                    context.renderContext.renderer->BeginGpuDetailTiming("Volumetric Fog / Temporal resolve");
        DrawFullscreenTriangle();
        if (temporalTiming)
            context.renderContext.renderer->EndGpuDetailTiming();

        // Preserve the depth corresponding to this fog history so the next
        // frame can reject disocclusions rather than dragging shafts over
        // newly revealed surfaces.
        Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, depthSource->GetFramebufferID());
        Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, historyWrite->GetFramebufferID());
        const bool historyCopyTiming = context.renderContext.renderer &&
                                       context.renderContext.renderer->BeginGpuDetailTiming("Volumetric Fog / Depth history copy");
        glBlitFramebuffer(0, 0, depthSource->GetWidth(), depthSource->GetHeight(),
                          0, 0, m_internalWidth, m_internalHeight,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        if (historyCopyTiming)
            context.renderContext.renderer->EndGpuDetailTiming();
        m_previousViewProjection = currentViewProjection;
        m_historyIndex = 1 - m_historyIndex;
        m_hasHistory = true;

        BeginApply(context);
        Graphics::SetViewport(0, 0, context.destinationRenderTarget->GetWidth(), context.destinationRenderTarget->GetHeight());
        Graphics::Disable(GL_BLEND);

        m_compositeShader->Bind();
        BindCommonInputs(m_compositeShader, context);
        Graphics::ActiveTexture(GL_TEXTURE5);
        Graphics::BindTexture(GL_TEXTURE_2D, historyWrite->GetColorTextureID());
        m_compositeShader->SetUniform("uFogTexture", 5);
        Graphics::ActiveTexture(GL_TEXTURE6);
        Graphics::BindTexture(GL_TEXTURE_2D, depthSource->GetDepthTextureID());
        m_compositeShader->SetUniform("uSceneDepthTexture", 6);
        m_compositeShader->SetUniform("uFogTexelSize", glm::vec2(1.0f / static_cast<float>(m_internalWidth),
                                                                  1.0f / static_cast<float>(m_internalHeight)));
        const bool compositeTiming = context.renderContext.renderer &&
                                     context.renderContext.renderer->BeginGpuDetailTiming("Volumetric Fog / Composite");
        DrawFullscreenTriangle();
        if (compositeTiming)
            context.renderContext.renderer->EndGpuDetailTiming();

        EndApply();
        glDepthMask(GL_TRUE);
    }
}
