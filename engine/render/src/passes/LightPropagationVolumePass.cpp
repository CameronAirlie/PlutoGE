#include "PlutoGE/render/passes/LightPropagationVolumePass.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
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
        constexpr float kPropagationSelfWeight = 0.36f;
        constexpr float kPropagationNeighborWeight = 0.07f;
        constexpr float kInjectionBoost = 1.8f;
        constexpr float kForwardBiasFactor = 0.25f;
        constexpr float kRecenterHysteresisFraction = 0.25f;
        constexpr float kTemporalBlendFactor = 0.2f;
        constexpr float kReprojectedTemporalBlendFactor = 0.12f;
        constexpr float kInjectionMovementUpdateFraction = 0.5f;
        constexpr float kInjectionRotationUpdateThreshold = 0.04f;
        constexpr auto kMovementDrivenUpdateInterval = std::chrono::milliseconds(80);
        constexpr auto kCameraOnlyReinjectionInterval = std::chrono::milliseconds(260);
        constexpr auto kGridTransitionDuration = std::chrono::milliseconds(140);

        bool IsLpvEffectEnabled(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return false;
            }

            for (const auto *effect : *ctx.postProcessEffects)
            {
                if (effect && effect->IsEnabled() && effect->GetTypeName() == "LPV")
                {
                    return true;
                }
            }

            return false;
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
                                         const glm::vec3 &gridSize)
        {
            return cameraPosition + cameraForward * (gridSize.z * kForwardBiasFactor);
        }

        glm::vec3 ComputeCameraCenteredGridOrigin(const glm::vec3 &cameraPosition,
                                                  const glm::vec3 &cameraForward,
                                                  const glm::vec3 &gridSize,
                                                  const glm::ivec3 &resolution)
        {
            const glm::vec3 lpvCenter = ComputeBiasedLpvCenter(cameraPosition, cameraForward, gridSize);
            const glm::vec3 rawOrigin = lpvCenter - gridSize * 0.5f;
            return SnapOriginToCell(rawOrigin, gridSize, resolution);
        }

        glm::vec3 ComputeHysteresisAdjustedOrigin(const glm::vec3 &cameraPosition,
                                                  const glm::vec3 &cameraForward,
                                                  const glm::vec3 &currentOrigin,
                                                  const glm::vec3 &gridSize,
                                                  const glm::ivec3 &resolution,
                                                  bool hasValidVolume)
        {
            const glm::vec3 snappedOrigin = ComputeCameraCenteredGridOrigin(cameraPosition, cameraForward, gridSize, resolution);
            if (!hasValidVolume)
            {
                return snappedOrigin;
            }

            const glm::vec3 biasedCenter = ComputeBiasedLpvCenter(cameraPosition, cameraForward, gridSize);
            const glm::vec3 hysteresisMargin = gridSize * kRecenterHysteresisFraction;
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
        m_hasValidVolume = false;
    }

    bool LightPropagationVolumePass::ShouldUpdateVolume(const RenderContext &ctx,
                                                        const glm::vec3 &desiredGridOrigin,
                                                        const glm::vec3 &desiredGridSize,
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

        return sceneChanged || lightsChanged || viewportChanged || gridChanged;
    }

    void LightPropagationVolumePass::Execute(const RenderContext &ctx)
    {
        if (!IsLpvEffectEnabled(ctx) || !ctx.hasCameraData || !ctx.gBuffer || !ctx.lights)
        {
            ClearVolume();
            return;
        }

        EnsureResources();

        const glm::vec3 cameraPosition = GetCameraPosition(ctx.cameraData);
        const glm::vec3 cameraForward = GetCameraForward(ctx.cameraData);
        const float horizontalExtent = glm::clamp(ctx.cameraData.farPlane * 0.4f, 20.0f, 64.0f);
        const float verticalExtent = glm::clamp(ctx.cameraData.farPlane * 0.22f, 12.0f, 28.0f);
        const glm::vec3 desiredGridSize(horizontalExtent, verticalExtent, horizontalExtent);
        const glm::vec3 desiredGridOrigin = ComputeHysteresisAdjustedOrigin(
            cameraPosition,
            cameraForward,
            m_gridOrigin,
            desiredGridSize,
            m_resolution,
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
        if (!ShouldUpdateVolume(ctx, desiredGridOrigin, desiredGridSize, sceneSignature, lightSignature))
        {
            return;
        }

        const glm::vec3 previousGridOrigin = m_gridOrigin;
        const glm::vec3 previousGridSize = m_gridSize;
        const bool sceneChanged = sceneSignature != m_lastSceneSignature;
        const bool lightsChanged = lightSignature != m_lastLightSignature;
        const bool viewportChanged = m_lastViewportSize != glm::ivec2(width, height);
        const bool sameGrid = m_hasValidVolume &&
                              !glm::any(glm::greaterThan(glm::abs(desiredGridOrigin - m_gridOrigin), glm::vec3(0.01f))) &&
                              !glm::any(glm::greaterThan(glm::abs(desiredGridSize - m_gridSize), glm::vec3(0.01f)));
        const bool sameGridSize = m_hasValidVolume &&
                                  !glm::any(glm::greaterThan(glm::abs(desiredGridSize - m_gridSize), glm::vec3(0.01f)));
        const bool gridShifted = sameGridSize &&
                                 glm::any(glm::greaterThan(glm::abs(desiredGridOrigin - m_gridOrigin), glm::vec3(0.01f)));
        const bool cameraOnlyGridShift = gridShifted && !sceneChanged && !lightsChanged && !viewportChanged;
        const auto now = std::chrono::steady_clock::now();
        const bool shouldRefreshCameraOnlyInjection =
            cameraOnlyGridShift &&
            (m_lastFullInjectionTime.time_since_epoch().count() == 0 || now - m_lastFullInjectionTime >= kCameraOnlyReinjectionInterval);
        const bool canBlendTemporalHistory = m_hasValidVolume && sameGridSize;
        const std::vector<glm::vec3> reprojectedHistoryRadiance = gridShifted
                                                                      ? ReprojectHistoryRadiance(m_historyRadiance, previousGridOrigin, previousGridSize, desiredGridOrigin, desiredGridSize, m_resolution)
                                                                      : std::vector<glm::vec3>();

        m_gridSize = desiredGridSize;
        m_gridOrigin = desiredGridOrigin;

        if (gridShifted && m_previousVolumeTexture && !m_historyRadiance.empty())
        {
            m_previousVolumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_historyRadiance.data());
            m_previousGridOrigin = previousGridOrigin;
            m_previousGridSize = previousGridSize;
            m_transitionStartTime = std::chrono::steady_clock::now();
            m_transitionActive = true;
        }
        else if (!gridShifted)
        {
            m_transitionActive = false;
            m_previousGridOrigin = m_gridOrigin;
            m_previousGridSize = m_gridSize;
        }

        if (cameraOnlyGridShift && !shouldRefreshCameraOnlyInjection && !reprojectedHistoryRadiance.empty())
        {
            m_currentRadiance = reprojectedHistoryRadiance;
            if (m_volumeTexture)
            {
                m_volumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_currentRadiance.data());
            }

            m_historyRadiance = m_currentRadiance;
            m_lastViewportSize = glm::ivec2(width, height);
            m_lastSceneSignature = sceneSignature;
            m_lastLightSignature = lightSignature;
            m_lastInjectionCameraPosition = cameraPosition;
            m_lastInjectionCameraForward = cameraForward;
            m_lastVolumeUpdateTime = std::chrono::steady_clock::now();
            m_hasValidVolume = true;
            return;
        }

        std::fill(m_currentRadiance.begin(), m_currentRadiance.end(), glm::vec3(0.0f));
        std::fill(m_nextRadiance.begin(), m_nextRadiance.end(), glm::vec3(0.0f));
        std::fill(m_injectionWeights.begin(), m_injectionWeights.end(), 0.0f);

        m_positionReadback.resize(static_cast<std::size_t>(width * height * 3));
        m_normalReadback.resize(static_cast<std::size_t>(width * height * 4));
        m_albedoReadback.resize(static_cast<std::size_t>(width * height * 4));

        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetPositionTextureID());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, m_positionReadback.data());
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetNormalTextureID());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, m_normalReadback.data());
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetAlbedoTextureID());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_albedoReadback.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        const int sampleStep = glm::max(1, glm::max(width, height) / 160);
        for (int y = 0; y < height; y += sampleStep)
        {
            for (int x = 0; x < width; x += sampleStep)
            {
                const std::size_t pixelIndex = static_cast<std::size_t>(y * width + x);
                const std::size_t positionIndex = pixelIndex * 3;
                const std::size_t normalIndex = pixelIndex * 4;
                const std::size_t albedoIndex = pixelIndex * 4;

                const glm::vec3 worldPosition(
                    m_positionReadback[positionIndex + 0],
                    m_positionReadback[positionIndex + 1],
                    m_positionReadback[positionIndex + 2]);
                const glm::vec3 normal(
                    m_normalReadback[normalIndex + 0],
                    m_normalReadback[normalIndex + 1],
                    m_normalReadback[normalIndex + 2]);

                if (glm::dot(normal, normal) <= 0.01f)
                {
                    continue;
                }

                const glm::vec3 albedo(
                    static_cast<float>(m_albedoReadback[albedoIndex + 0]) / 255.0f,
                    static_cast<float>(m_albedoReadback[albedoIndex + 1]) / 255.0f,
                    static_cast<float>(m_albedoReadback[albedoIndex + 2]) / 255.0f);
                const float metallic = static_cast<float>(m_albedoReadback[albedoIndex + 3]) / 255.0f;
                const glm::vec3 injectedRadiance = ComputeInjectedRadiance(worldPosition, normal, albedo, metallic, *ctx.lights);
                if (glm::dot(injectedRadiance, injectedRadiance) <= 0.000001f)
                {
                    continue;
                }

                glm::ivec3 cell(0);
                if (!WorldToCell(worldPosition, m_gridOrigin, m_gridSize, m_resolution, cell))
                {
                    continue;
                }

                const std::size_t cellIndex = FlattenCellIndex(m_resolution, cell.x, cell.y, cell.z);
                m_currentRadiance[cellIndex] += injectedRadiance;
                m_injectionWeights[cellIndex] += 1.0f;
            }
        }

        for (std::size_t cellIndex = 0; cellIndex < m_currentRadiance.size(); ++cellIndex)
        {
            if (m_injectionWeights[cellIndex] > 0.0f)
            {
                m_currentRadiance[cellIndex] = (m_currentRadiance[cellIndex] / m_injectionWeights[cellIndex]) * kInjectionBoost;
            }
        }

        for (int iteration = 0; iteration < kPropagationIterations; ++iteration)
        {
            for (int z = 0; z < m_resolution.z; ++z)
            {
                for (int y = 0; y < m_resolution.y; ++y)
                {
                    for (int x = 0; x < m_resolution.x; ++x)
                    {
                        const std::size_t cellIndex = FlattenCellIndex(m_resolution, x, y, z);
                        glm::vec3 propagatedRadiance = m_currentRadiance[cellIndex] * kPropagationSelfWeight;

                        if (x > 0)
                        {
                            propagatedRadiance += m_currentRadiance[FlattenCellIndex(m_resolution, x - 1, y, z)] * kPropagationNeighborWeight;
                        }
                        if (x + 1 < m_resolution.x)
                        {
                            propagatedRadiance += m_currentRadiance[FlattenCellIndex(m_resolution, x + 1, y, z)] * kPropagationNeighborWeight;
                        }
                        if (y > 0)
                        {
                            propagatedRadiance += m_currentRadiance[FlattenCellIndex(m_resolution, x, y - 1, z)] * kPropagationNeighborWeight;
                        }
                        if (y + 1 < m_resolution.y)
                        {
                            propagatedRadiance += m_currentRadiance[FlattenCellIndex(m_resolution, x, y + 1, z)] * kPropagationNeighborWeight;
                        }
                        if (z > 0)
                        {
                            propagatedRadiance += m_currentRadiance[FlattenCellIndex(m_resolution, x, y, z - 1)] * kPropagationNeighborWeight;
                        }
                        if (z + 1 < m_resolution.z)
                        {
                            propagatedRadiance += m_currentRadiance[FlattenCellIndex(m_resolution, x, y, z + 1)] * kPropagationNeighborWeight;
                        }

                        m_nextRadiance[cellIndex] = propagatedRadiance;
                    }
                }
            }

            m_currentRadiance.swap(m_nextRadiance);
        }

        // Temporal accumulation stabilizes injection and propagation while the snapped volume stays put.
        if (canBlendTemporalHistory)
        {
            const auto &historySource = gridShifted ? reprojectedHistoryRadiance : m_historyRadiance;
            BlendTemporalHistory(m_currentRadiance, historySource, sameGrid ? kTemporalBlendFactor : kReprojectedTemporalBlendFactor);
        }

        if (m_volumeTexture)
        {
            m_volumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_currentRadiance.data());
        }

        if (m_transitionActive && GetTransitionBlendFactor() >= 0.999f)
        {
            m_transitionActive = false;
            if (m_previousVolumeTexture)
            {
                m_previousVolumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_currentRadiance.data());
            }
            m_previousGridOrigin = m_gridOrigin;
            m_previousGridSize = m_gridSize;
        }

        m_historyRadiance = m_currentRadiance;

        m_lastViewportSize = glm::ivec2(width, height);
        m_lastSceneSignature = sceneSignature;
        m_lastLightSignature = lightSignature;
        m_lastInjectionCameraPosition = cameraPosition;
        m_lastInjectionCameraForward = cameraForward;
        m_lastVolumeUpdateTime = now;
        m_lastFullInjectionTime = now;
        m_hasValidVolume = true;
    }
}