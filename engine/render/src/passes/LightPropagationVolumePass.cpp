#include "PlutoGE/render/passes/LightPropagationVolumePass.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"
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

        glm::vec3 ComputeCameraCenteredGridOrigin(const glm::vec3 &cameraPosition,
                                                  const glm::vec3 &cameraForward,
                                                  const glm::vec3 &gridSize,
                                                  const glm::ivec3 &resolution)
        {
            const glm::vec3 lpvCenter = cameraPosition + cameraForward * (gridSize.z * kForwardBiasFactor);
            const glm::vec3 rawOrigin = lpvCenter - gridSize * 0.5f;
            return SnapOriginToCell(rawOrigin, gridSize, resolution);
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

        if (m_currentRadiance.size() != voxelCount)
        {
            m_currentRadiance.assign(voxelCount, glm::vec3(0.0f));
            m_nextRadiance.assign(voxelCount, glm::vec3(0.0f));
            m_injectionWeights.assign(voxelCount, 0.0f);
            m_hasValidVolume = false;
        }
    }

    void LightPropagationVolumePass::ClearVolume()
    {
        EnsureResources();
        std::fill(m_currentRadiance.begin(), m_currentRadiance.end(), glm::vec3(0.0f));
        std::fill(m_nextRadiance.begin(), m_nextRadiance.end(), glm::vec3(0.0f));
        std::fill(m_injectionWeights.begin(), m_injectionWeights.end(), 0.0f);
        if (m_volumeTexture)
        {
            m_volumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_currentRadiance.data());
        }
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
        if (!ctx.hasCameraData || !ctx.gBuffer || !ctx.lights)
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
        const glm::vec3 desiredGridOrigin = ComputeCameraCenteredGridOrigin(cameraPosition, cameraForward, desiredGridSize, m_resolution);

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

        m_gridSize = desiredGridSize;
        m_gridOrigin = desiredGridOrigin;

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

        if (m_volumeTexture)
        {
            m_volumeTexture->Upload3D(GL_RGB, GL_FLOAT, m_currentRadiance.data());
        }

        m_lastViewportSize = glm::ivec2(width, height);
        m_lastSceneSignature = sceneSignature;
        m_lastLightSignature = lightSignature;
        m_hasValidVolume = true;
    }
}