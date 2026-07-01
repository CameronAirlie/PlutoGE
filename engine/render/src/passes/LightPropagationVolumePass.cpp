#include "PlutoGE/render/passes/LightPropagationVolumePass.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/LPVEffect.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kPropagationIterations = 6;
        constexpr int kInjectionSampleGridX = 96;
        constexpr int kInjectionSampleGridY = 54;
        constexpr int kInjectionSampleCount = kInjectionSampleGridX * kInjectionSampleGridY;
        constexpr float kPropagationSelfWeight = 0.36f;
        constexpr float kPropagationNeighborWeight = 0.07f;
        constexpr float kInjectionBoost = 1.8f;
        constexpr float kTemporalBlendFactor = 0.2f;
        constexpr float kReprojectedTemporalBlendFactor = 0.12f;
        constexpr float kGpuTemporalBlendFactor = 0.18f;
        constexpr float kCameraMovementUpdateCellFraction = 0.5f;
        constexpr float kInjectionRotationUpdateThreshold = 0.04f;
        constexpr auto kMovementDrivenUpdateInterval = std::chrono::milliseconds(80);
        constexpr auto kCameraOnlyReinjectionInterval = std::chrono::milliseconds(260);
        constexpr auto kGridTransitionDuration = std::chrono::milliseconds(140);

        const LPVEffect *FindEnabledLpvEffect(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return nullptr;
            }

            for (const auto *effect : *ctx.postProcessEffects)
            {
                if (effect && effect->IsEnabled() && effect->GetTypeName() == "LPV")
                {
                    return static_cast<const LPVEffect *>(effect);
                }
            }

            return nullptr;
        }

        std::size_t HashBytes(const void *data, std::size_t size, std::size_t seed = 1469598103934665603ull)
        {
            constexpr std::size_t kPrime = 1099511628211ull;

            std::size_t hash = seed;
            const auto *bytes = static_cast<const std::uint8_t *>(data);
            for (std::size_t index = 0; index < size; ++index)
            {
                hash ^= static_cast<std::size_t>(bytes[index]);
                hash *= kPrime;
            }

            return hash;
        }

        template <typename T>
        std::size_t HashValue(const T &value, std::size_t seed)
        {
            return HashBytes(&value, sizeof(T), seed);
        }

        std::size_t FlattenCellIndex(const glm::ivec3 &resolution, int x, int y, int z)
        {
            return static_cast<std::size_t>(x + resolution.x * (y + resolution.y * z));
        }

        glm::vec3 GetCameraPosition(const CameraData &cameraData)
        {
            return glm::vec3(glm::inverse(cameraData.view)[3]);
        }

        glm::vec3 GetCameraForward(const CameraData &cameraData)
        {
            const glm::mat4 inverseView = glm::inverse(cameraData.view);
            const glm::vec3 forward = -glm::normalize(glm::vec3(inverseView[2]));
            if (glm::dot(forward, forward) <= 1e-8f)
            {
                return glm::vec3(0.0f, 0.0f, -1.0f);
            }

            return forward;
        }

        glm::vec3 SnapOriginToCell(const glm::vec3 &origin, const glm::vec3 &gridSize, const glm::ivec3 &resolution)
        {
            const glm::vec3 safeResolution = glm::max(glm::vec3(resolution), glm::vec3(1.0f));
            const glm::vec3 cellSize = glm::max(gridSize / safeResolution, glm::vec3(0.0001f));
            return glm::floor(origin / cellSize) * cellSize;
        }

        glm::vec3 ComputeBiasedLpvCenter(const glm::vec3 &cameraPosition,
                                         const glm::vec3 &cameraForward,
                                         const glm::vec3 &gridSize,
                                         float forwardBiasFactor)
        {
            return cameraPosition + cameraForward * (gridSize.z * forwardBiasFactor);
        }

        glm::vec3 ComputeCameraCenteredGridOrigin(const glm::vec3 &cameraPosition,
                                                  const glm::vec3 &cameraForward,
                                                  const glm::vec3 &gridSize,
                                                  const glm::ivec3 &resolution,
                                                  float forwardBiasFactor)
        {
            const glm::vec3 lpvCenter = ComputeBiasedLpvCenter(cameraPosition, cameraForward, gridSize, forwardBiasFactor);
            const glm::vec3 rawOrigin = lpvCenter - gridSize * 0.5f;
            return SnapOriginToCell(rawOrigin, gridSize, resolution);
        }

        glm::vec3 ComputeHysteresisAdjustedOrigin(const glm::vec3 &cameraPosition,
                                                  const glm::vec3 &cameraForward,
                                                  const glm::vec3 &currentOrigin,
                                                  const glm::vec3 &gridSize,
                                                  const glm::ivec3 &resolution,
                                                  float recenterHysteresisFraction,
                                                  float forwardBiasFactor,
                                                  bool hasValidVolume)
        {
            const glm::vec3 snappedOrigin = ComputeCameraCenteredGridOrigin(cameraPosition, cameraForward, gridSize, resolution, forwardBiasFactor);
            if (!hasValidVolume)
            {
                return snappedOrigin;
            }

            const glm::vec3 biasedCenter = ComputeBiasedLpvCenter(cameraPosition, cameraForward, gridSize, forwardBiasFactor);
            const glm::vec3 hysteresisMargin = gridSize * recenterHysteresisFraction;
            const glm::vec3 minCenter = currentOrigin + hysteresisMargin;
            const glm::vec3 maxCenter = currentOrigin + gridSize - hysteresisMargin;
            const bool centerInsideHysteresis =
                glm::all(glm::greaterThanEqual(biasedCenter, minCenter)) &&
                glm::all(glm::lessThanEqual(biasedCenter, maxCenter));

            return centerInsideHysteresis ? currentOrigin : snappedOrigin;
        }

        void BlendTemporalHistory(std::vector<glm::vec3> &currentRadiance,
                                  const std::vector<glm::vec3> &historyRadiance,
                                  float blendFactor)
        {
            if (historyRadiance.size() != currentRadiance.size() || blendFactor <= 0.0f)
            {
                return;
            }

            for (std::size_t cellIndex = 0; cellIndex < currentRadiance.size(); ++cellIndex)
            {
                currentRadiance[cellIndex] = glm::mix(historyRadiance[cellIndex], currentRadiance[cellIndex], blendFactor);
            }
        }

        glm::vec3 ComputeCellCenter(const glm::vec3 &origin,
                                    const glm::vec3 &gridSize,
                                    const glm::ivec3 &resolution,
                                    int x,
                                    int y,
                                    int z)
        {
            const glm::vec3 safeResolution = glm::max(glm::vec3(resolution), glm::vec3(1.0f));
            const glm::vec3 cellSize = gridSize / safeResolution;
            return origin + (glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) + glm::vec3(0.5f)) * cellSize;
        }

        glm::vec3 SampleReprojectedRadiance(const std::vector<glm::vec3> &historyRadiance,
                                            const glm::vec3 &historyOrigin,
                                            const glm::vec3 &historyGridSize,
                                            const glm::ivec3 &resolution,
                                            const glm::vec3 &worldPosition)
        {
            if (historyRadiance.empty())
            {
                return glm::vec3(0.0f);
            }

            const glm::vec3 safeHistorySize = glm::max(historyGridSize, glm::vec3(0.0001f));
            const glm::vec3 uvw = (worldPosition - historyOrigin) / safeHistorySize;
            if (glm::any(glm::lessThan(uvw, glm::vec3(0.0f))) || glm::any(glm::greaterThanEqual(uvw, glm::vec3(1.0f))))
            {
                return glm::vec3(0.0f);
            }

            const glm::vec3 scaled = uvw * glm::vec3(resolution) - glm::vec3(0.5f);
            const glm::ivec3 minCell = glm::clamp(glm::ivec3(glm::floor(scaled)), glm::ivec3(0), resolution - glm::ivec3(1));
            const glm::ivec3 maxCell = glm::clamp(minCell + glm::ivec3(1), glm::ivec3(0), resolution - glm::ivec3(1));
            const glm::vec3 fraction = glm::clamp(scaled - glm::floor(scaled), glm::vec3(0.0f), glm::vec3(1.0f));

            const glm::vec3 c000 = historyRadiance[FlattenCellIndex(resolution, minCell.x, minCell.y, minCell.z)];
            const glm::vec3 c100 = historyRadiance[FlattenCellIndex(resolution, maxCell.x, minCell.y, minCell.z)];
            const glm::vec3 c010 = historyRadiance[FlattenCellIndex(resolution, minCell.x, maxCell.y, minCell.z)];
            const glm::vec3 c110 = historyRadiance[FlattenCellIndex(resolution, maxCell.x, maxCell.y, minCell.z)];
            const glm::vec3 c001 = historyRadiance[FlattenCellIndex(resolution, minCell.x, minCell.y, maxCell.z)];
            const glm::vec3 c101 = historyRadiance[FlattenCellIndex(resolution, maxCell.x, minCell.y, maxCell.z)];
            const glm::vec3 c011 = historyRadiance[FlattenCellIndex(resolution, minCell.x, maxCell.y, maxCell.z)];
            const glm::vec3 c111 = historyRadiance[FlattenCellIndex(resolution, maxCell.x, maxCell.y, maxCell.z)];

            const glm::vec3 c00 = glm::mix(c000, c100, fraction.x);
            const glm::vec3 c10 = glm::mix(c010, c110, fraction.x);
            const glm::vec3 c01 = glm::mix(c001, c101, fraction.x);
            const glm::vec3 c11 = glm::mix(c011, c111, fraction.x);
            const glm::vec3 c0 = glm::mix(c00, c10, fraction.y);
            const glm::vec3 c1 = glm::mix(c01, c11, fraction.y);
            return glm::mix(c0, c1, fraction.z);
        }

        std::vector<glm::vec3> ReprojectHistoryRadiance(const std::vector<glm::vec3> &historyRadiance,
                                                        const glm::vec3 &historyOrigin,
                                                        const glm::vec3 &historyGridSize,
                                                        const glm::vec3 &currentOrigin,
                                                        const glm::vec3 &currentGridSize,
                                                        const glm::ivec3 &resolution)
        {
            std::vector<glm::vec3> reprojected(historyRadiance.size(), glm::vec3(0.0f));
            for (int z = 0; z < resolution.z; ++z)
            {
                for (int y = 0; y < resolution.y; ++y)
                {
                    for (int x = 0; x < resolution.x; ++x)
                    {
                        const std::size_t cellIndex = FlattenCellIndex(resolution, x, y, z);
                        const glm::vec3 worldCenter = ComputeCellCenter(currentOrigin, currentGridSize, resolution, x, y, z);
                        reprojected[cellIndex] = SampleReprojectedRadiance(historyRadiance, historyOrigin, historyGridSize, resolution, worldCenter);
                    }
                }
            }

            return reprojected;
        }

        std::size_t ComputeSceneSignature(const std::vector<RenderCommand> &renderCommands)
        {
            std::size_t hash = HashValue(renderCommands.size(), 1469598103934665603ull);
            for (const auto &command : renderCommands)
            {
                hash = HashValue(command.material, hash);
                hash = HashValue(command.mesh, hash);
                hash = HashValue(command.submeshIndex, hash);
                hash = HashValue(command.lodIndex, hash);
                hash = HashBytes(glm::value_ptr(command.model), sizeof(glm::mat4), hash);
            }

            return hash;
        }

        std::size_t ComputeLightSignature(const std::vector<scene::Light *> &lights)
        {
            std::size_t hash = HashValue(lights.size(), 1469598103934665603ull);
            for (const auto *light : lights)
            {
                hash = HashValue(light, hash);
                if (!light)
                {
                    continue;
                }

                hash = HashValue(light->type, hash);
                hash = HashBytes(glm::value_ptr(light->position), sizeof(glm::vec3), hash);
                hash = HashBytes(glm::value_ptr(light->direction), sizeof(glm::vec3), hash);
                hash = HashBytes(glm::value_ptr(light->color), sizeof(glm::vec3), hash);
                hash = HashValue(light->intensity, hash);
                hash = HashValue(light->range, hash);
                hash = HashValue(light->castsShadows, hash);
            }

            return hash;
        }

        bool WorldToCell(const glm::vec3 &worldPosition, const glm::vec3 &origin, const glm::vec3 &size, const glm::ivec3 &resolution, glm::ivec3 &cell)
        {
            const glm::vec3 safeSize = glm::max(size, glm::vec3(0.0001f));
            const glm::vec3 normalized = (worldPosition - origin) / safeSize;
            if (glm::any(glm::lessThan(normalized, glm::vec3(0.0f))) || glm::any(glm::greaterThanEqual(normalized, glm::vec3(1.0f))))
            {
                return false;
            }

            const glm::vec3 scaled = normalized * glm::vec3(resolution);
            cell = glm::clamp(glm::ivec3(scaled), glm::ivec3(0), resolution - glm::ivec3(1));
            return true;
        }

        float ComputePointAttenuation(const glm::vec3 &fragPos, const scene::Light &light)
        {
            const float distanceToLight = glm::length(light.position - fragPos);
            const float normalizedDistance = light.range > 0.0001f ? distanceToLight / light.range : 1.0f;
            const float attenuation = glm::clamp(1.0f - normalizedDistance, 0.0f, 1.0f);
            return attenuation * attenuation;
        }

        float ComputeSpotAttenuation(const glm::vec3 &fragPos, const glm::vec3 &lightDir, const scene::Light &light)
        {
            const float distanceAttenuation = ComputePointAttenuation(fragPos, light);
            const float spotEffect = glm::dot(-lightDir, glm::normalize(light.direction));
            return distanceAttenuation * glm::smoothstep(0.9f, 0.975f, spotEffect);
        }

        glm::vec3 ComputeInjectedRadiance(const glm::vec3 &fragPos, const glm::vec3 &normal, const glm::vec3 &albedo, float metallic, const std::vector<scene::Light *> &lights)
        {
            glm::vec3 totalRadiance(0.0f);
            const glm::vec3 surfaceNormal = glm::normalize(normal);
            const float diffuseReflectance = glm::clamp(1.0f - metallic, 0.0f, 1.0f);

            for (auto *light : lights)
            {
                if (!light)
                {
                    continue;
                }

                glm::vec3 lightDir(0.0f);
                float attenuation = 1.0f;
                if (light->type == scene::LightType::Directional)
                {
                    lightDir = glm::normalize(-light->direction);
                }
                else if (light->type == scene::LightType::Point)
                {
                    lightDir = glm::normalize(light->position - fragPos);
                    attenuation = ComputePointAttenuation(fragPos, *light);
                }
                else
                {
                    lightDir = glm::normalize(light->position - fragPos);
                    attenuation = ComputeSpotAttenuation(fragPos, lightDir, *light);
                }

                const float ndotl = glm::max(glm::dot(surfaceNormal, lightDir), 0.0f);
                if (ndotl <= 0.0f || attenuation <= 0.0f)
                {
                    continue;
                }

                totalRadiance += light->color * light->intensity * attenuation * ndotl;
            }

            return totalRadiance * albedo * diffuseReflectance;
        }

        float ComputeCameraMovementThreshold(const glm::vec3 &gridSize, const glm::ivec3 &resolution)
        {
            const glm::vec3 cellSize = gridSize / glm::max(glm::vec3(resolution), glm::vec3(1.0f));
            return glm::max(0.5f, glm::min(glm::min(cellSize.x, cellSize.y), cellSize.z) * kCameraMovementUpdateCellFraction);
        }
    }

    LightPropagationVolumePass::~LightPropagationVolumePass()
    {
        ReleaseGpuPassResources();
    }

    float LightPropagationVolumePass::GetTransitionBlendFactor() const
    {
        if (!m_transitionActive || !m_previousVolumeTexture)
        {
            return 1.0f;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - m_transitionStartTime;
        const float blend = std::clamp(
            static_cast<float>(std::chrono::duration_cast<std::chrono::duration<float>>(elapsed).count() /
                               std::chrono::duration_cast<std::chrono::duration<float>>(kGridTransitionDuration).count()),
            0.0f,
            1.0f);
        return blend;
    }

    void LightPropagationVolumePass::Initialize()
    {
        EnsureResources();
        ClearVolume();
    }

    void LightPropagationVolumePass::EnsureResources()
    {
        const std::size_t voxelCount = static_cast<std::size_t>(m_resolution.x * m_resolution.y * m_resolution.z);
        if (!m_volumeTexture ||
            m_volumeTexture->GetType() != GL_TEXTURE_3D ||
            m_volumeTexture->GetWidth() != m_resolution.x ||
            m_volumeTexture->GetHeight() != m_resolution.y ||
            m_volumeTexture->GetDepth() != m_resolution.z)
        {
            m_volumeTexture.reset(Texture::ColorVolume(m_resolution.x, m_resolution.y, m_resolution.z));
        }

        if (!m_previousVolumeTexture ||
            m_previousVolumeTexture->GetType() != GL_TEXTURE_3D ||
            m_previousVolumeTexture->GetWidth() != m_resolution.x ||
            m_previousVolumeTexture->GetHeight() != m_resolution.y ||
            m_previousVolumeTexture->GetDepth() != m_resolution.z)
        {
            m_previousVolumeTexture.reset(Texture::ColorVolume(m_resolution.x, m_resolution.y, m_resolution.z));
            m_transitionActive = false;
        }

        if (m_currentRadiance.size() != voxelCount)
        {
            m_currentRadiance.assign(voxelCount, glm::vec3(0.0f));
            m_historyRadiance.assign(voxelCount, glm::vec3(0.0f));
            m_nextRadiance.assign(voxelCount, glm::vec3(0.0f));
            m_injectionWeights.assign(voxelCount, 0.0f);
            m_hasValidVolume = false;
        }
    }

    void LightPropagationVolumePass::ClearVolume()
    {
        EnsureResources();
        std::fill(m_currentRadiance.begin(), m_currentRadiance.end(), glm::vec3(0.0f));
        std::fill(m_historyRadiance.begin(), m_historyRadiance.end(), glm::vec3(0.0f));
        std::fill(m_nextRadiance.begin(), m_nextRadiance.end(), glm::vec3(0.0f));
        std::fill(m_injectionWeights.begin(), m_injectionWeights.end(), 0.0f);
        if (m_volumeTexture)
        {
            m_volumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_currentRadiance.data());
        }
        if (m_previousVolumeTexture)
        {
            m_previousVolumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_currentRadiance.data());
        }
        m_previousGridOrigin = m_gridOrigin;
        m_previousGridSize = m_gridSize;
        m_transitionActive = false;
        m_pendingFullInjection = false;
        m_hasValidVolume = false;
    }

    void LightPropagationVolumePass::EnsureGpuPassResources()
    {
        if (!m_volumeFramebuffer)
        {
            glGenFramebuffers(1, &m_volumeFramebuffer);
        }
        if (!m_fullscreenVao)
        {
            glGenVertexArrays(1, &m_fullscreenVao);
        }

        if (!m_propagationTexture ||
            m_propagationTexture->GetType() != GL_TEXTURE_3D ||
            m_propagationTexture->GetWidth() != m_resolution.x ||
            m_propagationTexture->GetHeight() != m_resolution.y ||
            m_propagationTexture->GetDepth() != m_resolution.z)
        {
            m_propagationTexture.reset(Texture::ColorVolume(m_resolution.x, m_resolution.y, m_resolution.z));
        }

        if (!m_injectionShader)
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                const int MAX_LIGHTS = 16;
                const int SAMPLE_GRID_X = 96;
                const int SAMPLE_GRID_Y = 54;

                uniform sampler2D uPositionTexture;
                uniform sampler2D uNormalTexture;
                uniform sampler2D uAlbedoTexture;
                uniform vec3 uGridOrigin;
                uniform vec3 uGridSize;
                uniform vec3 uResolution;
                uniform int uLayer;
                uniform int uLightCount;
                uniform int uLightType[MAX_LIGHTS];
                uniform vec3 uLightPosition[MAX_LIGHTS];
                uniform vec3 uLightDirection[MAX_LIGHTS];
                uniform vec3 uLightColor[MAX_LIGHTS];
                uniform float uLightIntensity[MAX_LIGHTS];
                uniform float uLightRange[MAX_LIGHTS];

                out vec4 vContribution;

                float PointAttenuation(vec3 fragPos, int lightIndex)
                {
                    float distanceToLight = length(uLightPosition[lightIndex] - fragPos);
                    float normalizedDistance = uLightRange[lightIndex] > 0.0001 ? distanceToLight / uLightRange[lightIndex] : 1.0;
                    float attenuation = clamp(1.0 - normalizedDistance, 0.0, 1.0);
                    return attenuation * attenuation;
                }

                vec3 InjectedRadiance(vec3 fragPos, vec3 normal, vec3 albedo, float metallic)
                {
                    vec3 totalRadiance = vec3(0.0);
                    vec3 surfaceNormal = normalize(normal);
                    float diffuseReflectance = clamp(1.0 - metallic, 0.0, 1.0);

                    for (int lightIndex = 0; lightIndex < MAX_LIGHTS; ++lightIndex)
                    {
                        if (lightIndex >= uLightCount)
                        {
                            break;
                        }

                        vec3 lightDir = vec3(0.0);
                        float attenuation = 1.0;
                        if (uLightType[lightIndex] == 1)
                        {
                            lightDir = normalize(-uLightDirection[lightIndex]);
                        }
                        else
                        {
                            lightDir = normalize(uLightPosition[lightIndex] - fragPos);
                            attenuation = PointAttenuation(fragPos, lightIndex);
                            if (uLightType[lightIndex] == 2)
                            {
                                float spotEffect = dot(-lightDir, normalize(uLightDirection[lightIndex]));
                                attenuation *= smoothstep(0.9, 0.975, spotEffect);
                            }
                        }

                        float ndotl = max(dot(surfaceNormal, lightDir), 0.0);
                        if (ndotl <= 0.0 || attenuation <= 0.0)
                        {
                            continue;
                        }

                        totalRadiance += uLightColor[lightIndex] * uLightIntensity[lightIndex] * attenuation * ndotl;
                    }

                    return totalRadiance * albedo * diffuseReflectance;
                }

                void main()
                {
                    vContribution = vec4(0.0);
                    gl_PointSize = 1.0;
                    gl_Position = vec4(2.0, 2.0, 0.0, 1.0);

                    int sampleX = gl_VertexID % SAMPLE_GRID_X;
                    int sampleY = gl_VertexID / SAMPLE_GRID_X;
                    vec2 sampleUv = (vec2(sampleX, sampleY) + vec2(0.5)) / vec2(SAMPLE_GRID_X, SAMPLE_GRID_Y);

                    vec3 worldPosition = texture(uPositionTexture, sampleUv).rgb;
                    vec3 normal = texture(uNormalTexture, sampleUv).rgb;
                    if (dot(normal, normal) <= 0.01)
                    {
                        return;
                    }

                    vec3 safeGridSize = max(uGridSize, vec3(0.0001));
                    vec3 normalizedPosition = (worldPosition - uGridOrigin) / safeGridSize;
                    if (any(lessThan(normalizedPosition, vec3(0.0))) || any(greaterThanEqual(normalizedPosition, vec3(1.0))))
                    {
                        return;
                    }

                    ivec3 resolution = max(ivec3(uResolution), ivec3(1));
                    ivec3 cell = clamp(ivec3(normalizedPosition * uResolution), ivec3(0), resolution - ivec3(1));
                    if (cell.z != uLayer)
                    {
                        return;
                    }

                    vec4 albedoMetallic = texture(uAlbedoTexture, sampleUv);
                    vec3 radiance = InjectedRadiance(worldPosition, normal, albedoMetallic.rgb, albedoMetallic.a);
                    if (dot(radiance, radiance) <= 0.0)
                    {
                        return;
                    }

                    vec2 resolutionXY = max(uResolution.xy, vec2(1.0));
                    vec2 ndc = ((vec2(cell.xy) + vec2(0.5)) / resolutionXY) * 2.0 - 1.0;
                    gl_Position = vec4(ndc, 0.0, 1.0);
                    vContribution = vec4(radiance, 1.0);
                }
            )";

            source.fragmentSource = R"(
                #version 330 core
                out vec4 FragColor;

                in vec4 vContribution;

                void main()
                {
                    if (vContribution.a <= 0.0)
                    {
                        discard;
                    }

                    FragColor = vContribution;
                }
            )";

            m_injectionShader.reset(Shader::Create(source));
        }

        if (!m_injectionResolveShader)
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
                out vec4 FragColor;

                uniform sampler3D uInjectedVolume;
                uniform int uLayer;

                void main()
                {
                    ivec3 cell = ivec3(int(gl_FragCoord.x), int(gl_FragCoord.y), uLayer);
                    vec4 accumulated = texelFetch(uInjectedVolume, cell, 0);
                    float sampleWeight = accumulated.a;
                    vec3 averagedRadiance = sampleWeight > 0.0 ? (accumulated.rgb / sampleWeight) * 1.8 : vec3(0.0);
                    FragColor = vec4(averagedRadiance, sampleWeight > 0.0 ? 1.0 : 0.0);
                }
            )";

            m_injectionResolveShader.reset(Shader::Create(source));
        }

        if (!m_propagationShader)
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

                uniform sampler3D uSourceVolume;
                uniform vec3 uResolution;
                uniform int uLayer;

                const float SELF_WEIGHT = 0.36;
                const float NEIGHBOR_WEIGHT = 0.07;

                vec3 FetchCell(ivec3 cell)
                {
                    ivec3 resolution = ivec3(uResolution);
                    cell = clamp(cell, ivec3(0), resolution - ivec3(1));
                    return texelFetch(uSourceVolume, cell, 0).rgb;
                }

                void main()
                {
                    ivec3 cell = ivec3(int(gl_FragCoord.x), int(gl_FragCoord.y), uLayer);
                    vec3 radiance = FetchCell(cell) * SELF_WEIGHT;
                    radiance += FetchCell(cell + ivec3(-1, 0, 0)) * NEIGHBOR_WEIGHT;
                    radiance += FetchCell(cell + ivec3(1, 0, 0)) * NEIGHBOR_WEIGHT;
                    radiance += FetchCell(cell + ivec3(0, -1, 0)) * NEIGHBOR_WEIGHT;
                    radiance += FetchCell(cell + ivec3(0, 1, 0)) * NEIGHBOR_WEIGHT;
                    radiance += FetchCell(cell + ivec3(0, 0, -1)) * NEIGHBOR_WEIGHT;
                    radiance += FetchCell(cell + ivec3(0, 0, 1)) * NEIGHBOR_WEIGHT;
                    FragColor = vec4(radiance, 1.0);
                }
            )";

            m_propagationShader.reset(Shader::Create(source));
        }

        if (!m_blendShader)
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

                uniform sampler3D uCurrentVolume;
                uniform sampler3D uHistoryVolume;
                uniform vec3 uResolution;
                uniform int uLayer;
                uniform float uCurrentBlendFactor;

                void main()
                {
                    ivec3 cell = ivec3(int(gl_FragCoord.x), int(gl_FragCoord.y), uLayer);
                    vec3 currentRadiance = texelFetch(uCurrentVolume, cell, 0).rgb;
                    vec3 historyRadiance = texelFetch(uHistoryVolume, cell, 0).rgb;
                    vec3 blendedRadiance = mix(historyRadiance, currentRadiance, clamp(uCurrentBlendFactor, 0.0, 1.0));
                    FragColor = vec4(blendedRadiance, 1.0);
                }
            )";

            m_blendShader.reset(Shader::Create(source));
        }
    }

    void LightPropagationVolumePass::ReleaseGpuPassResources()
    {
        if (m_volumeFramebuffer)
        {
            glDeleteFramebuffers(1, &m_volumeFramebuffer);
            m_volumeFramebuffer = 0;
        }
        if (m_fullscreenVao)
        {
            glDeleteVertexArrays(1, &m_fullscreenVao);
            m_fullscreenVao = 0;
        }

        m_injectionShader.reset();
        m_injectionResolveShader.reset();
        m_propagationShader.reset();
        m_blendShader.reset();
        m_propagationTexture.reset();
    }

    void LightPropagationVolumePass::RenderGpuInjection(const RenderContext &ctx)
    {
        EnsureGpuPassResources();
        if (!m_injectionShader || !m_injectionResolveShader || !m_volumeTexture || !m_propagationTexture)
        {
            return;
        }

        GLint previousFramebuffer = 0;
        GLint previousViewport[4] = {};
        GLint previousBlendSrcRgb = GL_ONE;
        GLint previousBlendDstRgb = GL_ZERO;
        GLint previousBlendSrcAlpha = GL_ONE;
        GLint previousBlendDstAlpha = GL_ZERO;
        GLint previousBlendEquationRgb = GL_FUNC_ADD;
        GLint previousBlendEquationAlpha = GL_FUNC_ADD;
        GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &previousBlendEquationRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &previousBlendEquationAlpha);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, m_volumeFramebuffer);
        glViewport(0, 0, m_resolution.x, m_resolution.y);
        glBindVertexArray(m_fullscreenVao);

        constexpr GLfloat kClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int layer = 0; layer < m_resolution.z; ++layer)
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_volumeTexture->GetTextureID(), 0, layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glClearBufferfv(GL_COLOR, 0, kClearColor);
        }

        m_injectionShader->Bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetPositionTextureID());
        m_injectionShader->SetUniform("uPositionTexture", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetNormalTextureID());
        m_injectionShader->SetUniform("uNormalTexture", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetAlbedoTextureID());
        m_injectionShader->SetUniform("uAlbedoTexture", 2);
        m_injectionShader->SetUniform("uGridOrigin", m_gridOrigin);
        m_injectionShader->SetUniform("uGridSize", m_gridSize);
        m_injectionShader->SetUniform("uResolution", glm::vec3(m_resolution));

        const int lightCount = std::min<int>(ctx.lights ? static_cast<int>(ctx.lights->size()) : 0, 16);
        m_injectionShader->SetUniform("uLightCount", lightCount);
        for (int lightIndex = 0; lightIndex < lightCount; ++lightIndex)
        {
            const auto *light = (*ctx.lights)[lightIndex];
            const int lightType = light ? static_cast<int>(light->type) : 0;
            m_injectionShader->SetUniform("uLightType[" + std::to_string(lightIndex) + "]", lightType);
            m_injectionShader->SetUniform("uLightPosition[" + std::to_string(lightIndex) + "]", light ? light->position : glm::vec3(0.0f));
            m_injectionShader->SetUniform("uLightDirection[" + std::to_string(lightIndex) + "]", light ? light->direction : glm::vec3(0.0f, -1.0f, 0.0f));
            m_injectionShader->SetUniform("uLightColor[" + std::to_string(lightIndex) + "]", light ? light->color : glm::vec3(0.0f));
            m_injectionShader->SetUniform("uLightIntensity[" + std::to_string(lightIndex) + "]", light ? light->intensity : 0.0f);
            m_injectionShader->SetUniform("uLightRange[" + std::to_string(lightIndex) + "]", light ? light->range : 1.0f);
        }

        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
        for (int layer = 0; layer < m_resolution.z; ++layer)
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_volumeTexture->GetTextureID(), 0, layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            m_injectionShader->SetUniform("uLayer", layer);
            glDrawArrays(GL_POINTS, 0, kInjectionSampleCount);
        }

        glDisable(GL_BLEND);
        m_injectionResolveShader->Bind();
        m_injectionResolveShader->SetUniform("uInjectedVolume", m_volumeTexture.get(), 0);
        for (int layer = 0; layer < m_resolution.z; ++layer)
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_propagationTexture->GetTextureID(), 0, layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            m_injectionResolveShader->SetUniform("uLayer", layer);
            Graphics::DrawFullscreenTriangle();
        }

        m_volumeTexture.swap(m_propagationTexture);

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glBindVertexArray(0);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        glBlendFuncSeparate(previousBlendSrcRgb, previousBlendDstRgb, previousBlendSrcAlpha, previousBlendDstAlpha);
        glBlendEquationSeparate(previousBlendEquationRgb, previousBlendEquationAlpha);
        if (depthTestEnabled)
        {
            glEnable(GL_DEPTH_TEST);
        }
        if (blendEnabled)
        {
            glEnable(GL_BLEND);
        }
        Shader::ResetStateCache();
    }

    void LightPropagationVolumePass::RenderGpuPropagation()
    {
        EnsureGpuPassResources();
        if (!m_propagationShader || !m_volumeTexture || !m_propagationTexture)
        {
            return;
        }

        GLint previousFramebuffer = 0;
        GLint previousViewport[4] = {};
        GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, m_volumeFramebuffer);
        glViewport(0, 0, m_resolution.x, m_resolution.y);
        glBindVertexArray(m_fullscreenVao);

        m_propagationShader->Bind();
        m_propagationShader->SetUniform("uResolution", glm::vec3(m_resolution));

        for (int iteration = 0; iteration < kPropagationIterations; ++iteration)
        {
            m_propagationShader->SetUniform("uSourceVolume", m_volumeTexture.get(), 0);
            for (int layer = 0; layer < m_resolution.z; ++layer)
            {
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_propagationTexture->GetTextureID(), 0, layer);
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                if (layer == 0 && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    break;
                }

                m_propagationShader->SetUniform("uLayer", layer);
                Graphics::DrawFullscreenTriangle();
            }

            m_volumeTexture.swap(m_propagationTexture);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glBindVertexArray(0);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (depthTestEnabled)
        {
            glEnable(GL_DEPTH_TEST);
        }
        if (blendEnabled)
        {
            glEnable(GL_BLEND);
        }
        Shader::ResetStateCache();
    }

    void LightPropagationVolumePass::RenderGpuVolumeBlend(Texture *currentVolume, Texture *historyVolume, Texture *targetVolume, float currentBlendFactor)
    {
        EnsureGpuPassResources();
        if (!m_blendShader || !currentVolume || !historyVolume || !targetVolume)
        {
            return;
        }

        GLint previousFramebuffer = 0;
        GLint previousViewport[4] = {};
        GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, m_volumeFramebuffer);
        glViewport(0, 0, m_resolution.x, m_resolution.y);
        glBindVertexArray(m_fullscreenVao);

        m_blendShader->Bind();
        m_blendShader->SetUniform("uCurrentVolume", currentVolume, 0);
        m_blendShader->SetUniform("uHistoryVolume", historyVolume, 1);
        m_blendShader->SetUniform("uResolution", glm::vec3(m_resolution));
        m_blendShader->SetUniform("uCurrentBlendFactor", currentBlendFactor);

        for (int layer = 0; layer < m_resolution.z; ++layer)
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetVolume->GetTextureID(), 0, layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            if (layer == 0 && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                break;
            }

            m_blendShader->SetUniform("uLayer", layer);
            Graphics::DrawFullscreenTriangle();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glBindVertexArray(0);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (depthTestEnabled)
        {
            glEnable(GL_DEPTH_TEST);
        }
        if (blendEnabled)
        {
            glEnable(GL_BLEND);
        }
        Shader::ResetStateCache();
    }

    bool LightPropagationVolumePass::ShouldUpdateVolume(const RenderContext &ctx,
                                                        const glm::vec3 &desiredGridOrigin,
                                                        const glm::vec3 &desiredGridSize,
                                                        const glm::vec3 &cameraPosition,
                                                        const glm::vec3 &cameraForward,
                                                        std::size_t sceneSignature,
                                                        std::size_t lightSignature)
    {
        if (!m_hasValidVolume)
        {
            return true;
        }

        const bool sceneChanged = sceneSignature != m_lastSceneSignature;
        const bool lightsChanged = lightSignature != m_lastLightSignature;
        const bool viewportChanged = m_lastViewportSize != glm::ivec2(ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight());
        const bool gridChanged = glm::any(glm::greaterThan(glm::abs(desiredGridSize - m_gridSize), glm::vec3(0.01f))) ||
                                 glm::any(glm::greaterThan(glm::abs(desiredGridOrigin - m_gridOrigin), glm::vec3(0.01f)));
        const float cameraMovementThreshold = ComputeCameraMovementThreshold(desiredGridSize, m_resolution);
        const bool cameraMoved = glm::distance(cameraPosition, m_lastInjectionCameraPosition) >= cameraMovementThreshold;
        const float forwardAlignment = glm::clamp(glm::dot(cameraForward, m_lastInjectionCameraForward), -1.0f, 1.0f);
        const bool cameraRotated = (1.0f - forwardAlignment) >= kInjectionRotationUpdateThreshold;
        const bool cameraChanged = cameraMoved || cameraRotated;
        const auto now = std::chrono::steady_clock::now();
        const bool pendingFullInjectionReady =
            m_pendingFullInjection &&
            (m_lastFullInjectionTime.time_since_epoch().count() == 0 || now - m_lastFullInjectionTime >= kCameraOnlyReinjectionInterval);

        if (!sceneChanged && !lightsChanged && !viewportChanged && !gridChanged && !cameraChanged && !pendingFullInjectionReady)
        {
            return false;
        }

        if (lightsChanged || viewportChanged)
        {
            return true;
        }

        if (m_lastVolumeUpdateTime.time_since_epoch().count() != 0 && now - m_lastVolumeUpdateTime < kMovementDrivenUpdateInterval)
        {
            return false;
        }

        return sceneChanged || gridChanged || cameraChanged || pendingFullInjectionReady;
    }

    void LightPropagationVolumePass::Execute(const RenderContext &ctx)
    {
        const auto *lpvEffect = FindEnabledLpvEffect(ctx);
        if (!lpvEffect || !ctx.hasCameraData || !ctx.gBuffer || !ctx.lights)
        {
            ClearVolume();
            return;
        }

        const int desiredResolution = std::max(lpvEffect->GetGridResolution(), 1);
        if (m_resolution.x != desiredResolution || m_resolution.y != desiredResolution || m_resolution.z != desiredResolution)
        {
            m_resolution = glm::ivec3(desiredResolution);
        }

        EnsureResources();
        EnsureGpuPassResources();

        const glm::vec3 cameraPosition = GetCameraPosition(ctx.cameraData);
        const glm::vec3 cameraForward = GetCameraForward(ctx.cameraData);
        const float horizontalExtent = std::max(lpvEffect->GetMinimumHorizontalCoverage(), glm::clamp(ctx.cameraData.farPlane * 0.55f, 48.0f, 192.0f));
        const float verticalExtent = std::max(lpvEffect->GetMinimumVerticalCoverage(), glm::clamp(ctx.cameraData.farPlane * 0.30f, 24.0f, 96.0f));
        const glm::vec3 desiredGridSize(horizontalExtent, verticalExtent, horizontalExtent);
        const glm::vec3 desiredGridOrigin = ComputeHysteresisAdjustedOrigin(
            cameraPosition,
            cameraForward,
            m_gridOrigin,
            desiredGridSize,
            m_resolution,
            lpvEffect->GetRecenterHysteresisFraction(),
            lpvEffect->GetForwardBiasFactor(),
            m_hasValidVolume && !glm::any(glm::greaterThan(glm::abs(desiredGridSize - m_gridSize), glm::vec3(0.01f))));

        const int width = ctx.gBuffer->GetWidth();
        const int height = ctx.gBuffer->GetHeight();
        if (width <= 0 || height <= 0)
        {
            ClearVolume();
            return;
        }

        const std::size_t sceneSignature = ctx.renderCommands ? ComputeSceneSignature(*ctx.renderCommands) : 0;
        const std::size_t lightSignature = ComputeLightSignature(*ctx.lights);
        if (!ShouldUpdateVolume(ctx, desiredGridOrigin, desiredGridSize, cameraPosition, cameraForward, sceneSignature, lightSignature))
        {
            return;
        }

        const glm::vec3 previousGridOrigin = m_gridOrigin;
        const glm::vec3 previousGridSize = m_gridSize;
        const bool sameGridSize = m_hasValidVolume &&
                                  !glm::any(glm::greaterThan(glm::abs(desiredGridSize - m_gridSize), glm::vec3(0.01f)));
        const bool gridShifted = sameGridSize &&
                                 glm::any(glm::greaterThan(glm::abs(desiredGridOrigin - m_gridOrigin), glm::vec3(0.01f)));
        const bool canBlendSameGridHistory = m_hasValidVolume && !gridShifted && m_previousVolumeTexture && m_propagationTexture;
        const auto now = std::chrono::steady_clock::now();

        if (m_hasValidVolume && m_previousVolumeTexture)
        {
            RenderGpuVolumeBlend(m_volumeTexture.get(), m_volumeTexture.get(), m_previousVolumeTexture.get(), 1.0f);
        }

        m_gridSize = desiredGridSize;
        m_gridOrigin = desiredGridOrigin;

        if (gridShifted)
        {
            m_previousGridOrigin = previousGridOrigin;
            m_previousGridSize = previousGridSize;
            m_transitionStartTime = now;
            m_transitionActive = true;
        }
        else
        {
            m_transitionActive = false;
            m_previousGridOrigin = m_gridOrigin;
            m_previousGridSize = m_gridSize;
        }

        RenderGpuInjection(ctx);
        RenderGpuPropagation();

        if (canBlendSameGridHistory)
        {
            RenderGpuVolumeBlend(m_volumeTexture.get(), m_previousVolumeTexture.get(), m_propagationTexture.get(), kGpuTemporalBlendFactor);
            m_volumeTexture.swap(m_propagationTexture);
        }

        m_lastViewportSize = glm::ivec2(width, height);
        m_lastSceneSignature = sceneSignature;
        m_lastLightSignature = lightSignature;
        m_lastInjectionCameraPosition = cameraPosition;
        m_lastInjectionCameraForward = cameraForward;
        m_lastVolumeUpdateTime = now;
        m_lastFullInjectionTime = now;
        m_pendingFullInjection = false;
        m_hasValidVolume = true;
    }
}
