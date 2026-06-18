#include "PlutoGE/render/passes/ShadowPass.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace
{
    constexpr int kProjectedShadowPassMode = 0;
    constexpr int kPointShadowPassMode = 1;
    constexpr int kMaxIncrementalShadowSurfaceUpdatesPerFrame = 2;
    constexpr float kDirectionalShadowPadding = 2.0f;
    constexpr float kShadowUpdateMatrixEpsilon = 0.0001f;

    struct FrustumPlane
    {
        glm::vec3 normal{0.0f};
        float distance = 0.0f;
    };

    struct TransformInstanceData
    {
        glm::mat4 model{1.0f};
    };

    glm::vec3 ResolveUpVector(const glm::vec3 &direction)
    {
        return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    struct ShadowCasterBounds
    {
        glm::vec3 center{0.0f};
        float radius = 10.0f;
    };

    struct ShadowCasterEntry
    {
        const PlutoGE::render::RenderCommand *command = nullptr;
        PlutoGE::render::MeshBounds bounds;
        PlutoGE::render::MeshBounds previousBounds;
        bool hasMoved = false;
    };

    struct DirectionalCascadeProjection
    {
        glm::mat4 lightSpaceMatrix{1.0f};
        glm::mat4 lightViewMatrix{1.0f};
        glm::vec2 receiverMin{0.0f};
        glm::vec2 receiverMax{0.0f};
        glm::vec2 receiverExtent{1.0f};
    };

    struct ShadowDrawStats
    {
        int submittedInstances = 0;
        int submittedBatches = 0;
    };

    int GetDirectionalCascadeCount(const PlutoGE::scene::Light &light)
    {
        return std::clamp(light.activeShadowCascadeCount, 1, PlutoGE::scene::kDefaultDirectionalShadowCascades);
    }

    int GetShadowResolution(const PlutoGE::scene::Light &light)
    {
        return std::max(light.directionalShadowSettings.resolution, 256);
    }

    bool AreMatricesApproximatelyEqual(const glm::mat4 &a, const glm::mat4 &b, float epsilon = kShadowUpdateMatrixEpsilon)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (std::abs(a[column][row] - b[column][row]) > epsilon)
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool HasCameraDataChanged(const PlutoGE::render::CameraData &current, const PlutoGE::render::CameraData &previous)
    {
        return !AreMatricesApproximatelyEqual(current.view, previous.view) ||
               !AreMatricesApproximatelyEqual(current.projection, previous.projection) ||
               std::abs(current.nearPlane - previous.nearPlane) > kShadowUpdateMatrixEpsilon ||
               std::abs(current.farPlane - previous.farPlane) > kShadowUpdateMatrixEpsilon;
    }

    bool ShouldRefreshCameraRelativeCascade(const glm::vec3 &currentOrigin,
                                            const glm::vec3 &storedOrigin,
                                            float cascadeNear,
                                            float cascadeFar)
    {
        const float cascadeThickness = glm::max(cascadeFar - cascadeNear, 0.1f);
        const float recenterThreshold = glm::max(0.5f, cascadeThickness * 0.25f);
        return glm::distance(currentOrigin, storedOrigin) > recenterThreshold;
    }

    bool ShouldUpdateDirectionalCascade(std::uint64_t frameSequence, int cascadeIndex, bool forceFullUpdate, bool motionDrivenInvalidation)
    {
        if (forceFullUpdate)
        {
            return true;
        }

        const int cadenceOffset = motionDrivenInvalidation ? 3 : 1;
        const int cadenceIndex = std::clamp(cascadeIndex + cadenceOffset, cadenceOffset, 6);
        const std::uint64_t cadence = 1ull << static_cast<std::uint64_t>(cadenceIndex);
        if (!motionDrivenInvalidation)
        {
            return (frameSequence % cadence) == 0;
        }

        // Phase-shift motion-driven cascade refreshes so farther cascades do not pile onto
        // the same frame as the near cascade and cause periodic present-time spikes.
        const std::uint64_t phaseOffset = (cadence >> 1) - 1ull;
        return ((frameSequence + phaseOffset) % cadence) == 0;
    }

    void ConfigureMatrixAttributes(unsigned int baseLocation, std::size_t offset, std::size_t stride)
    {
        for (unsigned int column = 0; column < 4; ++column)
        {
            const unsigned int location = baseLocation + column;
            glEnableVertexAttribArray(location);
            glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<const void *>(offset + sizeof(glm::vec4) * column));
            glVertexAttribDivisor(location, 1);
        }
    }

    void BindTransformInstanceAttributes(const PlutoGE::render::Mesh &mesh, unsigned int instanceBuffer)
    {
        glBindVertexArray(mesh.GetVAO());
        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        ConfigureMatrixAttributes(5, offsetof(TransformInstanceData, model), sizeof(TransformInstanceData));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void UploadTransformInstances(unsigned int &instanceBuffer,
                                  std::size_t &instanceCapacity,
                                  const std::vector<TransformInstanceData> &instances)
    {
        if (instances.empty())
        {
            return;
        }

        if (instanceBuffer == 0)
        {
            glGenBuffers(1, &instanceBuffer);
        }

        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        if (instanceCapacity < instances.size())
        {
            instanceCapacity = std::max(instances.size(), instanceCapacity == 0 ? instances.size() : instanceCapacity * 2);
        }

        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instanceCapacity * sizeof(TransformInstanceData)), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instances.size() * sizeof(TransformInstanceData)), instances.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    bool CanBatchShadowCommands(const PlutoGE::render::RenderCommand &a, const PlutoGE::render::RenderCommand &b)
    {
        if (a.jointMatrices || b.jointMatrices)
        {
            return false;
        }

        const bool aAlphaTested = a.material && a.material->GetConfig().albedoTexture && a.material->GetConfig().alphaMode == PlutoGE::render::AlphaMode::Mask;
        const bool bAlphaTested = b.material && b.material->GetConfig().albedoTexture && b.material->GetConfig().alphaMode == PlutoGE::render::AlphaMode::Mask;
        if (aAlphaTested != bAlphaTested)
        {
            return false;
        }

        return (!aAlphaTested || a.material == b.material) &&
               a.mesh == b.mesh &&
               a.submeshIndex == b.submeshIndex &&
               a.lodIndex == b.lodIndex;
    }

    bool IsAlphaTestedShadowCaster(const PlutoGE::render::RenderCommand &command)
    {
        return command.material && command.material->GetConfig().albedoTexture && command.material->GetConfig().alphaMode == PlutoGE::render::AlphaMode::Mask;
    }

    bool CastsShadow(const PlutoGE::render::RenderCommand &command)
    {
        return command.material &&
               command.material->GetConfig().castsShadow &&
               command.material->GetConfig().alphaMode != PlutoGE::render::AlphaMode::Blend;
    }

    void HashCombine(std::uint64_t &seed, std::uint64_t value)
    {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    }

    std::uint64_t QuantizeFloatForHash(float value)
    {
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::round(value * 1000.0f)));
    }

    bool ValidateShadowFramebuffer(const char *label)
    {
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status == GL_FRAMEBUFFER_COMPLETE)
        {
            return true;
        }

        std::cerr << "Shadow framebuffer incomplete for " << label << ": 0x" << std::hex << status << std::dec << std::endl;
        return false;
    }

    void BuildShadowCasterEntries(const std::vector<PlutoGE::render::RenderCommand> &renderCommands,
                                  std::vector<ShadowCasterEntry> &shadowCasters,
                                  bool &shadowCastersChanged)
    {
        shadowCasters.clear();
        shadowCasters.reserve(renderCommands.size());
        shadowCastersChanged = false;

        for (const auto &command : renderCommands)
        {
            if (!command.mesh || !command.material)
            {
                continue;
            }

            if (!CastsShadow(command))
            {
                continue;
            }

            const bool hasMoved = !AreMatricesApproximatelyEqual(command.model, command.previousModel);
            shadowCastersChanged = shadowCastersChanged || hasMoved;
            shadowCasters.push_back(ShadowCasterEntry{
                .command = &command,
                .bounds = command.worldBounds,
                .previousBounds = command.previousWorldBounds,
                .hasMoved = hasMoved,
            });
        }
    }

    std::uint64_t ComputeShadowCasterFingerprint(const std::vector<ShadowCasterEntry> &shadowCasters)
    {
        std::uint64_t fingerprint = 1469598103934665603ull;
        HashCombine(fingerprint, static_cast<std::uint64_t>(shadowCasters.size()));

        for (const auto &shadowCaster : shadowCasters)
        {
            const auto *command = shadowCaster.command;
            HashCombine(fingerprint, reinterpret_cast<std::uintptr_t>(command ? command->mesh : nullptr));
            HashCombine(fingerprint, reinterpret_cast<std::uintptr_t>(command ? command->material : nullptr));
            HashCombine(fingerprint, static_cast<std::uint64_t>(command ? command->submeshIndex : 0));
            HashCombine(fingerprint, static_cast<std::uint64_t>(command ? command->lodIndex : 0));
            HashCombine(fingerprint, QuantizeFloatForHash(shadowCaster.bounds.center.x));
            HashCombine(fingerprint, QuantizeFloatForHash(shadowCaster.bounds.center.y));
            HashCombine(fingerprint, QuantizeFloatForHash(shadowCaster.bounds.center.z));
            HashCombine(fingerprint, QuantizeFloatForHash(shadowCaster.bounds.radius));
        }

        return fingerprint;
    }

    std::vector<const ShadowCasterEntry *> BuildSortedShadowCasters(const std::vector<ShadowCasterEntry> &shadowCasters)
    {
        std::vector<const ShadowCasterEntry *> sortedShadowCasters;
        sortedShadowCasters.reserve(shadowCasters.size());
        for (const auto &shadowCaster : shadowCasters)
        {
            sortedShadowCasters.push_back(&shadowCaster);
        }

        std::sort(sortedShadowCasters.begin(), sortedShadowCasters.end(), [](const auto *a, const auto *b)
                  {
                      const auto *aCommand = a->command;
                      const auto *bCommand = b->command;
                      const bool aAlphaTested = IsAlphaTestedShadowCaster(*aCommand);
                      const bool bAlphaTested = IsAlphaTestedShadowCaster(*bCommand);
                      if (aAlphaTested != bAlphaTested)
                      {
                          return aAlphaTested < bAlphaTested;
                      }
                      if (aAlphaTested && aCommand->material != bCommand->material)
                      {
                          return std::less<PlutoGE::render::Material *>{}(aCommand->material, bCommand->material);
                      }
                      if (aCommand->mesh != bCommand->mesh)
                      {
                          return std::less<PlutoGE::render::Mesh *>{}(aCommand->mesh, bCommand->mesh);
                      }
                      if (aCommand->submeshIndex != bCommand->submeshIndex)
                      {
                          return aCommand->submeshIndex < bCommand->submeshIndex;
                      }
                      return aCommand->lodIndex < bCommand->lodIndex;
                  });
        return sortedShadowCasters;
    }

    void ExpandDirectionalCascadeDepthBounds(
        const glm::mat4 &lightView,
        const std::vector<ShadowCasterEntry> &shadowCasters,
        const glm::vec3 &shadowWorldOrigin,
        glm::vec3 &minBounds,
        glm::vec3 &maxBounds)
    {
        const glm::vec2 receiverMin(minBounds.x, minBounds.y);
        const glm::vec2 receiverMax(maxBounds.x, maxBounds.y);

        for (const auto &shadowCaster : shadowCasters)
        {
            const PlutoGE::render::MeshBounds &bounds = shadowCaster.bounds;
            const glm::vec3 relativeCenter = bounds.center - shadowWorldOrigin;
            const glm::vec3 lightSpaceCenter = glm::vec3(lightView * glm::vec4(relativeCenter, 1.0f));
            const float radius = glm::max(bounds.radius, 0.001f);

            if (lightSpaceCenter.x + radius < receiverMin.x ||
                lightSpaceCenter.x - radius > receiverMax.x ||
                lightSpaceCenter.y + radius < receiverMin.y ||
                lightSpaceCenter.y - radius > receiverMax.y)
            {
                continue;
            }

            minBounds.z = glm::min(minBounds.z, lightSpaceCenter.z - radius - kDirectionalShadowPadding);
            maxBounds.z = glm::max(maxBounds.z, lightSpaceCenter.z + radius + kDirectionalShadowPadding);
        }
    }

    std::array<glm::vec3, 8> BuildCameraFrustumCorners(const PlutoGE::render::CameraData &cameraData)
    {
        const glm::mat4 inverseViewProjection = glm::inverse(cameraData.projection * cameraData.view);
        std::array<glm::vec3, 8> corners{};
        std::size_t index = 0;

        for (int z = 0; z < 2; ++z)
        {
            const float clipZ = z == 0 ? -1.0f : 1.0f;
            for (int y = 0; y < 2; ++y)
            {
                const float clipY = y == 0 ? -1.0f : 1.0f;
                for (int x = 0; x < 2; ++x)
                {
                    const float clipX = x == 0 ? -1.0f : 1.0f;
                    const glm::vec4 worldCorner = inverseViewProjection * glm::vec4(clipX, clipY, clipZ, 1.0f);
                    corners[index++] = glm::vec3(worldCorner) / worldCorner.w;
                }
            }
        }

        return corners;
    }

    std::array<glm::vec3, 8> BuildCascadeFrustumCorners(const PlutoGE::render::CameraData &cameraData, float cascadeNear, float cascadeFar)
    {
        const auto frustumCorners = BuildCameraFrustumCorners(cameraData);
        std::array<glm::vec3, 8> cascadeCorners{};
        const float nearDepth = glm::max(cascadeNear, cameraData.nearPlane);
        const float farDepth = glm::max(cascadeFar, nearDepth + 0.1f);
        const float cameraDepthRange = glm::max(cameraData.farPlane - cameraData.nearPlane, 0.0001f);
        const float nearFactor = glm::clamp((nearDepth - cameraData.nearPlane) / cameraDepthRange, 0.0f, 1.0f);
        const float farFactor = glm::clamp((farDepth - cameraData.nearPlane) / cameraDepthRange, 0.0f, 1.0f);

        for (std::size_t index = 0; index < 4; ++index)
        {
            const glm::vec3 &cameraNearCorner = frustumCorners[index];
            const glm::vec3 &cameraFarCorner = frustumCorners[index + 4];
            cascadeCorners[index] = cameraNearCorner + (cameraFarCorner - cameraNearCorner) * nearFactor;
            cascadeCorners[index + 4] = cameraNearCorner + (cameraFarCorner - cameraNearCorner) * farFactor;
        }

        return cascadeCorners;
    }

    std::array<float, PlutoGE::scene::kMaxDirectionalShadowCascades> BuildDirectionalCascadeSplits(
        const PlutoGE::render::CameraData &cameraData,
        const PlutoGE::scene::DirectionalShadowSettings &settings,
        int cascadeCount)
    {
        std::array<float, PlutoGE::scene::kMaxDirectionalShadowCascades> splits{};
        const float nearRadius = glm::max(cameraData.nearPlane, 0.1f);
        const float shadowDistance = settings.maxDistance > 0.0f ? settings.maxDistance : cameraData.farPlane;
        const float farRadius = glm::min(cameraData.farPlane, glm::max(shadowDistance, nearRadius + 0.1f));
        const float lambda = glm::clamp(settings.splitLambda, 0.0f, 1.0f);

        for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            const float splitFactor = static_cast<float>(cascadeIndex + 1) / static_cast<float>(cascadeCount);
            const float logarithmicSplit = nearRadius * std::pow(farRadius / nearRadius, splitFactor);
            const float uniformSplit = nearRadius + (farRadius - nearRadius) * splitFactor;
            splits[cascadeIndex] = glm::mix(uniformSplit, logarithmicSplit, lambda);
        }

        if (cascadeCount > 1 && settings.nearCascadeDistance > 0.0f)
        {
            const float nearCascadeFar = glm::clamp(settings.nearCascadeDistance, nearRadius + 0.1f, farRadius - 0.1f);
            splits[0] = glm::min(splits[0], nearCascadeFar);
        }

        for (int cascadeIndex = cascadeCount; cascadeIndex < PlutoGE::scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
        {
            splits[cascadeIndex] = farRadius;
        }

        return splits;
    }

    void SnapDirectionalProjectionBoundsToTexels(glm::vec3 &minBounds, glm::vec3 &maxBounds, int shadowResolution)
    {
        const float safeResolution = static_cast<float>(std::max(shadowResolution, 1));
        const glm::vec2 extents = glm::max(glm::vec2(maxBounds.x - minBounds.x, maxBounds.y - minBounds.y), glm::vec2(0.001f));
        const glm::vec2 texelSize = extents / safeResolution;
        const glm::vec2 center = (glm::vec2(minBounds.x, minBounds.y) + glm::vec2(maxBounds.x, maxBounds.y)) * 0.5f;
        const glm::vec2 snappedCenter = glm::round(center / texelSize) * texelSize;
        const glm::vec2 halfExtents = extents * 0.5f;

        minBounds.x = snappedCenter.x - halfExtents.x;
        maxBounds.x = snappedCenter.x + halfExtents.x;
        minBounds.y = snappedCenter.y - halfExtents.y;
        maxBounds.y = snappedCenter.y + halfExtents.y;
    }

    void SnapDirectionalProjectionMatrixToTexels(glm::mat4 &projection,
                                                 const glm::mat4 &view,
                                                 const glm::vec3 &shadowWorldOrigin,
                                                 int shadowResolution)
    {
        const float safeResolution = static_cast<float>(std::max(shadowResolution, 1));
        const glm::mat4 lightSpaceMatrix = projection * view;
        (void)shadowWorldOrigin;
        const glm::vec4 stableWorldAnchor = lightSpaceMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const glm::vec2 shadowTexelOrigin = glm::vec2(stableWorldAnchor) * (safeResolution * 0.5f);
        const glm::vec2 roundedTexelOrigin = glm::round(shadowTexelOrigin);
        const glm::vec2 shadowTexelOffset = (roundedTexelOrigin - shadowTexelOrigin) * (2.0f / safeResolution);

        projection[3][0] += shadowTexelOffset.x;
        projection[3][1] += shadowTexelOffset.y;
    }

    DirectionalCascadeProjection BuildDirectionalCascadeProjection(
        const PlutoGE::scene::Light &light,
        const PlutoGE::render::CameraData &cameraData,
        const std::vector<ShadowCasterEntry> &shadowCasters,
        const glm::vec3 &shadowWorldOrigin,
        float cascadeNear,
        float cascadeFar,
        int shadowResolution)
    {
        const glm::vec3 lightDirection = glm::normalize(light.direction);
        const auto worldCascadeCorners = BuildCascadeFrustumCorners(cameraData, cascadeNear, cascadeFar);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        const glm::vec3 cascadeCenter = cameraPosition - shadowWorldOrigin;
        float cascadeRadius = 0.0f;
        for (const glm::vec3 &corner : worldCascadeCorners)
        {
            cascadeRadius = glm::max(cascadeRadius, glm::distance(corner, cameraPosition));
        }
        cascadeRadius = glm::max(cascadeRadius, cascadeFar);

        const float cascadeDepth = glm::max(cascadeFar - cascadeNear, 1.0f);
        const float casterExtrusionDistance = cascadeDepth * 2.0f + kDirectionalShadowPadding;
        const glm::vec3 upVector = ResolveUpVector(lightDirection);

        const float pcfGuardTexels = glm::max(light.directionalShadowSettings.softness + 1.0f, 2.0f);

        glm::vec3 eye = cascadeCenter;

        auto computeCascadeBounds = [&](const glm::mat4 &view, glm::vec3 &minBounds, glm::vec3 &maxBounds)
        {
            const glm::vec3 lightSpaceCenter = glm::vec3(view * glm::vec4(cascadeCenter, 1.0f));
            minBounds = lightSpaceCenter - glm::vec3(cascadeRadius);
            maxBounds = lightSpaceCenter + glm::vec3(cascadeRadius);

            const glm::vec3 extrudedCenter = cascadeCenter - lightDirection * casterExtrusionDistance;
            const glm::vec3 lightSpaceExtrudedCenter = glm::vec3(view * glm::vec4(extrudedCenter, 1.0f));
            minBounds.z = glm::min(minBounds.z, lightSpaceExtrudedCenter.z - cascadeRadius);
            maxBounds.z = glm::max(maxBounds.z, lightSpaceExtrudedCenter.z + cascadeRadius);

            const glm::vec2 receiverExtent = glm::max(glm::vec2(maxBounds.x - minBounds.x, maxBounds.y - minBounds.y), glm::vec2(0.001f));
            const glm::vec2 texelSize = receiverExtent / static_cast<float>(std::max(shadowResolution, 1));
            const glm::vec2 xyGuard = texelSize * pcfGuardTexels + glm::vec2(kDirectionalShadowPadding);
            minBounds -= glm::vec3(xyGuard.x, xyGuard.y, kDirectionalShadowPadding);
            maxBounds += glm::vec3(xyGuard.x, xyGuard.y, kDirectionalShadowPadding);
            ExpandDirectionalCascadeDepthBounds(view, shadowCasters, shadowWorldOrigin, minBounds, maxBounds);
        };

        glm::mat4 view = glm::lookAt(eye, eye + lightDirection, upVector);
        glm::vec3 minBounds(0.0f);
        glm::vec3 maxBounds(0.0f);
        computeCascadeBounds(view, minBounds, maxBounds);

        if (maxBounds.z > -kDirectionalShadowPadding)
        {
            const float retreatDistance = maxBounds.z + kDirectionalShadowPadding;
            eye -= lightDirection * retreatDistance;
            view = glm::lookAt(eye, eye + lightDirection, upVector);
            computeCascadeBounds(view, minBounds, maxBounds);
        }

        const float nearPlane = glm::max(0.1f, -maxBounds.z);
        const float farPlane = glm::max(nearPlane + 0.1f, -minBounds.z);
        SnapDirectionalProjectionBoundsToTexels(minBounds, maxBounds, shadowResolution);
        glm::mat4 projection = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, nearPlane, farPlane);
        SnapDirectionalProjectionMatrixToTexels(projection, view, shadowWorldOrigin, shadowResolution);
        return DirectionalCascadeProjection{
            .lightSpaceMatrix = projection * view,
            .lightViewMatrix = view,
            .receiverMin = glm::vec2(minBounds.x, minBounds.y),
            .receiverMax = glm::vec2(maxBounds.x, maxBounds.y),
            .receiverExtent = glm::max(glm::vec2(maxBounds.x - minBounds.x, maxBounds.y - minBounds.y), glm::vec2(0.001f)),
        };
    }

    glm::mat4 BuildSpotShadowMatrix(const PlutoGE::scene::Light &light)
    {
        const float farPlane = glm::max(light.range, 0.1f);
        const glm::vec3 lightDirection = glm::normalize(light.direction);
        const glm::mat4 view = glm::lookAt(light.position, light.position + lightDirection, ResolveUpVector(lightDirection));
        const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, farPlane);
        return projection * view;
    }

    std::array<glm::mat4, 6> BuildPointShadowMatrices(const PlutoGE::scene::Light &light, float farPlane)
    {
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
        const glm::vec3 position = light.position;

        return {
            projection * glm::lookAt(position, position + glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            projection * glm::lookAt(position, position + glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        };
    }

    std::array<FrustumPlane, 6> ExtractFrustumPlanes(const glm::mat4 &viewProjection)
    {
        std::array<FrustumPlane, 6> planes = {
            FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][0], viewProjection[1][3] + viewProjection[1][0], viewProjection[2][3] + viewProjection[2][0]), viewProjection[3][3] + viewProjection[3][0]},
            FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][0], viewProjection[1][3] - viewProjection[1][0], viewProjection[2][3] - viewProjection[2][0]), viewProjection[3][3] - viewProjection[3][0]},
            FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][1], viewProjection[1][3] + viewProjection[1][1], viewProjection[2][3] + viewProjection[2][1]), viewProjection[3][3] + viewProjection[3][1]},
            FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][1], viewProjection[1][3] - viewProjection[1][1], viewProjection[2][3] - viewProjection[2][1]), viewProjection[3][3] - viewProjection[3][1]},
            FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][2], viewProjection[1][3] + viewProjection[1][2], viewProjection[2][3] + viewProjection[2][2]), viewProjection[3][3] + viewProjection[3][2]},
            FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][2], viewProjection[1][3] - viewProjection[1][2], viewProjection[2][3] - viewProjection[2][2]), viewProjection[3][3] - viewProjection[3][2]},
        };

        for (auto &plane : planes)
        {
            const float length = glm::length(plane.normal);
            if (length > 1e-6f)
            {
                plane.normal /= length;
                plane.distance /= length;
            }
        }

        return planes;
    }

    bool IsBoundsVisible(const PlutoGE::render::MeshBounds &bounds, const std::array<FrustumPlane, 6> &planes)
    {
        for (const auto &plane : planes)
        {
            if (glm::dot(plane.normal, bounds.center) + plane.distance < -bounds.radius)
            {
                return false;
            }
        }

        return true;
    }

    bool IsBoundsRelevantForPointLight(const PlutoGE::render::MeshBounds &bounds, const PlutoGE::scene::Light &light)
    {
        const float maxDistance = glm::max(light.range, 0.1f) + bounds.radius;
        const glm::vec3 offset = bounds.center - light.position;
        return glm::dot(offset, offset) <= maxDistance * maxDistance;
    }

    bool IsCommandRelevantForPointLight(const ShadowCasterEntry &shadowCaster, const PlutoGE::scene::Light &light)
    {
        return IsBoundsRelevantForPointLight(shadowCaster.bounds, light);
    }

    bool IsMovedCommandRelevantForPointLight(const ShadowCasterEntry &shadowCaster, const PlutoGE::scene::Light &light)
    {
        return shadowCaster.hasMoved &&
               (IsBoundsRelevantForPointLight(shadowCaster.bounds, light) ||
                IsBoundsRelevantForPointLight(shadowCaster.previousBounds, light));
    }

    bool IsBoundsRelevantForProjectedLight(const PlutoGE::render::MeshBounds &bounds, const std::array<FrustumPlane, 6> &planes)
    {
        return IsBoundsVisible(bounds, planes);
    }

    bool IsCommandRelevantForProjectedLight(const ShadowCasterEntry &shadowCaster, const std::array<FrustumPlane, 6> &planes)
    {
        return IsBoundsRelevantForProjectedLight(shadowCaster.bounds, planes);
    }

    bool IsMovedCommandRelevantForProjectedLight(const ShadowCasterEntry &shadowCaster, const std::array<FrustumPlane, 6> &planes)
    {
        return shadowCaster.hasMoved &&
               (IsBoundsRelevantForProjectedLight(shadowCaster.bounds, planes) ||
                IsBoundsRelevantForProjectedLight(shadowCaster.previousBounds, planes));
    }

    bool IsBoundsRelevantForDirectionalCascade(const PlutoGE::render::MeshBounds &bounds,
                                               const glm::mat4 &lightView,
                                               const glm::vec3 &shadowWorldOrigin,
                                               const glm::vec2 &receiverMin,
                                               const glm::vec2 &receiverMax,
                                               const glm::vec2 &receiverExtent,
                                               int shadowResolution,
                                               float minCasterTexelRadius)
    {
        const glm::vec3 relativeCenter = bounds.center - shadowWorldOrigin;
        const glm::vec3 lightSpaceCenter = glm::vec3(lightView * glm::vec4(relativeCenter, 1.0f));
        const float radius = glm::max(bounds.radius, 0.001f);

        if (lightSpaceCenter.x + radius < receiverMin.x ||
            lightSpaceCenter.x - radius > receiverMax.x ||
            lightSpaceCenter.y + radius < receiverMin.y ||
            lightSpaceCenter.y - radius > receiverMax.y)
        {
            return false;
        }

        if (minCasterTexelRadius > 0.0f)
        {
            const glm::vec2 safeReceiverExtent = glm::max(receiverExtent, glm::vec2(0.001f));
            const float safeShadowResolution = static_cast<float>(std::max(shadowResolution, 1));
            const glm::vec2 texelSize = safeReceiverExtent / safeShadowResolution;
            const float maxTexelSize = glm::max(texelSize.x, texelSize.y);
            if (maxTexelSize > 0.000001f && radius / maxTexelSize < minCasterTexelRadius)
            {
                return false;
            }
        }

        return true;
    }

    bool IsCommandRelevantForDirectionalCascade(const ShadowCasterEntry &shadowCaster,
                                                const glm::mat4 &lightView,
                                                const glm::vec3 &shadowWorldOrigin,
                                                const glm::vec2 &receiverMin,
                                                const glm::vec2 &receiverMax,
                                                const glm::vec2 &receiverExtent,
                                                int shadowResolution,
                                                float minCasterTexelRadius)
    {
        return IsBoundsRelevantForDirectionalCascade(shadowCaster.bounds, lightView, shadowWorldOrigin, receiverMin, receiverMax, receiverExtent, shadowResolution, minCasterTexelRadius);
    }

    bool IsMovedCommandRelevantForDirectionalCascade(const ShadowCasterEntry &shadowCaster,
                                                    const glm::mat4 &lightView,
                                                    const glm::vec3 &shadowWorldOrigin,
                                                    const glm::vec2 &receiverMin,
                                                    const glm::vec2 &receiverMax,
                                                    const glm::vec2 &receiverExtent,
                                                    int shadowResolution,
                                                    float minCasterTexelRadius)
    {
        return shadowCaster.hasMoved &&
               (IsBoundsRelevantForDirectionalCascade(shadowCaster.bounds, lightView, shadowWorldOrigin, receiverMin, receiverMax, receiverExtent, shadowResolution, minCasterTexelRadius) ||
                IsBoundsRelevantForDirectionalCascade(shadowCaster.previousBounds, lightView, shadowWorldOrigin, receiverMin, receiverMax, receiverExtent, shadowResolution, minCasterTexelRadius));
    }

    template <typename Predicate>
    bool AnyMovedShadowCasterRelevant(const std::vector<ShadowCasterEntry> &shadowCasters, Predicate &&predicate)
    {
        return std::any_of(shadowCasters.begin(), shadowCasters.end(), [&](const ShadowCasterEntry &shadowCaster)
                           {
                               return shadowCaster.hasMoved && predicate(shadowCaster);
                           });
    }

    void BindShadowMaterialState(PlutoGE::render::Shader *shader, PlutoGE::render::Material *material)
    {
        if (!shader || !material)
        {
            return;
        }

        auto *albedoTexture = material->GetConfig().albedoTexture;
        const bool alphaTested = albedoTexture && material->GetConfig().color.a < 0.999f;
        if (alphaTested)
        {
            shader->SetUniform("uAlbedoTexture", albedoTexture, 0);
            shader->SetUniform("uHasAlbedoTexture", 1.0f);
            shader->SetUniform("uAlphaCutoff", material->GetConfig().alphaCutoff);
            return;
        }

        shader->SetUniform("uHasAlbedoTexture", 0.0f);
    }

    void UploadShadowJointMatrices(PlutoGE::render::Shader *shader, const std::vector<glm::mat4> *jointMatrices)
    {
        constexpr size_t kMaxShaderJoints = 48;
        if (!shader || !jointMatrices || jointMatrices->empty())
        {
            shader->SetUniform("uUseSkinning", 0);
            return;
        }

        shader->SetUniform("uUseSkinning", 1);
        const size_t jointCount = std::min(jointMatrices->size(), kMaxShaderJoints);
        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            shader->SetUniform(std::string("uJointMatrices[") + std::to_string(jointIndex) + "]", (*jointMatrices)[jointIndex]);
        }
    }

    template <typename Predicate>
    ShadowDrawStats DrawShadowCasterBatches(const std::vector<const ShadowCasterEntry *> &sortedShadowCasters,
                                            Predicate &&predicate,
                                            PlutoGE::render::Shader *shader,
                                            unsigned int &instanceBuffer,
                                            std::size_t &instanceCapacity,
                                            std::vector<TransformInstanceData> &batchInstances)
    {
        ShadowDrawStats stats;
        PlutoGE::render::Material *boundMaterial = nullptr;
        PlutoGE::render::Mesh *boundMesh = nullptr;
        batchInstances.clear();
        if (batchInstances.capacity() < 64)
        {
            batchInstances.reserve(64);
        }

        const auto flushBatch = [&](const PlutoGE::render::RenderCommand &batchHead)
        {
            if (batchInstances.empty())
            {
                return;
            }

            if (batchHead.material != boundMaterial)
            {
                if (IsAlphaTestedShadowCaster(batchHead))
                {
                    BindShadowMaterialState(shader, batchHead.material);
                }
                else
                {
                    shader->SetUniform("uHasAlbedoTexture", 0.0f);
                }
                boundMaterial = batchHead.material;
            }

            shader->SetUniform("uUseSkinning", 0);
            UploadTransformInstances(instanceBuffer, instanceCapacity, batchInstances);
            if (batchHead.mesh != boundMesh)
            {
                BindTransformInstanceAttributes(*batchHead.mesh, instanceBuffer);
                boundMesh = batchHead.mesh;
            }

            batchHead.mesh->DrawSubmeshInstancedBound(batchHead.submeshIndex, batchInstances.size(), batchHead.lodIndex);
            stats.submittedInstances += static_cast<int>(batchInstances.size());
            ++stats.submittedBatches;
            batchInstances.clear();
        };

        const PlutoGE::render::RenderCommand *batchHead = nullptr;
        for (const auto *shadowCaster : sortedShadowCasters)
        {
            if (!predicate(*shadowCaster))
            {
                continue;
            }

            const auto *command = shadowCaster->command;
            if (command->jointMatrices)
            {
                if (batchHead)
                {
                    flushBatch(*batchHead);
                    batchHead = nullptr;
                }

                if (command->material != boundMaterial)
                {
                    if (IsAlphaTestedShadowCaster(*command))
                    {
                        BindShadowMaterialState(shader, command->material);
                    }
                    else
                    {
                        shader->SetUniform("uHasAlbedoTexture", 0.0f);
                    }
                    boundMaterial = command->material;
                }

                UploadShadowJointMatrices(shader, command->jointMatrices);
                const std::vector<TransformInstanceData> singleInstance{
                    TransformInstanceData{
                        .model = command->model,
                    },
                };
                UploadTransformInstances(instanceBuffer, instanceCapacity, singleInstance);
                BindTransformInstanceAttributes(*command->mesh, instanceBuffer);
                boundMesh = command->mesh;
                command->mesh->DrawSubmeshInstancedBound(command->submeshIndex, 1, command->lodIndex);
                stats.submittedInstances += 1;
                ++stats.submittedBatches;
                shader->SetUniform("uUseSkinning", 0);
                continue;
            }

            if (batchHead && !CanBatchShadowCommands(*batchHead, *command))
            {
                flushBatch(*batchHead);
                batchHead = nullptr;
            }

            if (!batchHead)
            {
                batchHead = command;
            }

            batchInstances.push_back(TransformInstanceData{
                .model = command->model,
            });
        }

        if (batchHead)
        {
            flushBatch(*batchHead);
        }

        return stats;
    }
}

namespace PlutoGE::render
{
    void ShadowPass::Initialize()
    {
        m_shadowPassShader = Shader::CreateShadowPassShader();
        glGenFramebuffers(1, &m_shadowFramebuffer);
        if (m_instanceBuffer == 0)
        {
            glGenBuffers(1, &m_instanceBuffer);
        }
    }

    void ShadowPass::Execute(const RenderContext &ctx)
    {
        if (!m_shadowPassShader || !ctx.lights || !ctx.renderCommands || m_shadowFramebuffer == 0)
        {
            return;
        }

        GLint previousViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDepthRange(0.0, 1.0);
        glClearDepth(1.0);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_POLYGON_OFFSET_FILL);

        m_shadowPassShader->Bind();
        m_shadowPassShader->SetUniform("uShadowWorldOrigin", glm::vec3(0.0f));
        std::vector<ShadowCasterEntry> shadowCasters;
        bool shadowCastersChanged = false;
        BuildShadowCasterEntries(*ctx.renderCommands, shadowCasters, shadowCastersChanged);
        const std::uint64_t shadowCasterFingerprint = ComputeShadowCasterFingerprint(shadowCasters);
        if (!m_hasShadowCasterFingerprint || shadowCasterFingerprint != m_shadowCasterFingerprint)
        {
            shadowCastersChanged = true;
            m_shadowCasterFingerprint = shadowCasterFingerprint;
            m_hasShadowCasterFingerprint = true;
        }
        const auto sortedShadowCasters = BuildSortedShadowCasters(shadowCasters);
        const bool cameraDataChanged = ctx.hasCameraData && (!ctx.hasPreviousCameraData || HasCameraDataChanged(ctx.cameraData, ctx.previousCameraData));
        std::vector<TransformInstanceData> shadowBatchInstances;
        int incrementalShadowSurfaceUpdates = 0;
        auto reserveIncrementalShadowSurfaceUpdate = [&]()
        {
            if (incrementalShadowSurfaceUpdates >= kMaxIncrementalShadowSurfaceUpdatesPerFrame)
            {
                return false;
            }

            ++incrementalShadowSurfaceUpdates;
            return true;
        };

        for (auto *light : *ctx.lights)
        {
            if (!light || !light->castsShadows)
            {
                continue;
            }

            const bool hasPendingIncrementalRefresh = light->shadowRefreshPending && !light->isDirty;
            bool deferredShadowRefresh = false;
            const bool motionDrivenDirectionalInvalidation = light->type == scene::LightType::Directional && ctx.hasCameraData && (cameraDataChanged || shadowCastersChanged || hasPendingIncrementalRefresh);

            const bool needsUpdate = light->type == scene::LightType::Directional
                                         ? (ctx.hasCameraData &&
                                            (light->isDirty ||
                                             hasPendingIncrementalRefresh ||
                                             motionDrivenDirectionalInvalidation))
                                         : (light->isDirty || shadowCastersChanged || hasPendingIncrementalRefresh);
            if (!needsUpdate)
            {
                continue;
            }

            if (light->type == scene::LightType::Point)
            {
                auto *shadowMap = light->shadowMap.get();
                if (!shadowMap)
                {
                    continue;
                }

                glDisable(GL_POLYGON_OFFSET_FILL);
                const float farPlane = glm::max(light->range, 0.1f);
                light->shadowFarPlane = farPlane;
                const auto shadowMatrices = BuildPointShadowMatrices(*light, farPlane);
                glViewport(0, 0, shadowMap->GetWidth(), shadowMap->GetHeight());

                m_shadowPassShader->SetUniform("uShadowPassMode", kPointShadowPassMode);
                m_shadowPassShader->SetUniform("uLightPosition", light->position);
                m_shadowPassShader->SetUniform("uFarPlane", farPlane);

                for (unsigned int face = 0; face < shadowMatrices.size(); ++face)
                {
                    const auto faceFrustumPlanes = ExtractFrustumPlanes(shadowMatrices[face]);
                    bool faceNeedsIncrementalRefresh = hasPendingIncrementalRefresh;
                    if (!light->isDirty && !faceNeedsIncrementalRefresh)
                    {
                        faceNeedsIncrementalRefresh = AnyMovedShadowCasterRelevant(
                            shadowCasters,
                            [&](const ShadowCasterEntry &shadowCaster)
                            {
                                return IsMovedCommandRelevantForPointLight(shadowCaster, *light) &&
                                       IsMovedCommandRelevantForProjectedLight(shadowCaster, faceFrustumPlanes);
                            });
                    }

                    if (!light->isDirty)
                    {
                        if (!faceNeedsIncrementalRefresh)
                        {
                            continue;
                        }

                        if (!shadowCastersChanged && !reserveIncrementalShadowSurfaceUpdate())
                        {
                            deferredShadowRefresh = true;
                            continue;
                        }
                    }

                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, shadowMap->GetTextureID(), 0);
                    if (!ValidateShadowFramebuffer("point shadow"))
                    {
                        continue;
                    }
                    glClear(GL_DEPTH_BUFFER_BIT);
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrices[face]);
                    const ShadowDrawStats drawStats = DrawShadowCasterBatches(
                        sortedShadowCasters,
                        [&](const ShadowCasterEntry &shadowCaster)
                        {
                            return IsCommandRelevantForPointLight(shadowCaster, *light) &&
                                   IsCommandRelevantForProjectedLight(shadowCaster, faceFrustumPlanes);
                        },
                        m_shadowPassShader,
                        m_instanceBuffer,
                        m_instanceCapacity,
                        shadowBatchInstances);
                    if (ctx.renderer)
                    {
                        ctx.renderer->RecordShadowMapUpdate(
                            shadowMap->GetWidth() * shadowMap->GetHeight(),
                            drawStats.submittedInstances,
                            drawStats.submittedBatches,
                            false);
                    }
                }

                light->isDirty = false;
                light->shadowRefreshPending = deferredShadowRefresh;
                continue;
            }

            if (light->type == scene::LightType::Directional)
            {
                if (!ctx.hasCameraData)
                {
                    continue;
                }

                const glm::vec3 currentShadowWorldOrigin = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
                const int cascadeCount = GetDirectionalCascadeCount(*light);
                const auto cascadeSplits = BuildDirectionalCascadeSplits(ctx.cameraData, light->directionalShadowSettings, cascadeCount);
                const bool forceFullCascadeUpdate = light->isDirty || !ctx.hasPreviousCameraData;
                const bool motionDrivenCascadeInvalidation = !light->isDirty && (shadowCastersChanged || hasPendingIncrementalRefresh);
                const bool casterOnlyCascadeInvalidation = shadowCastersChanged && !light->isDirty && !cameraDataChanged && !hasPendingIncrementalRefresh;
                const bool cameraOnlyInvalidation = cameraDataChanged && !light->isDirty && !shadowCastersChanged;

                light->shadowMatrix = glm::mat4(1.0f);
                light->shadowFarPlane = cascadeSplits[cascadeCount - 1];
                glDisable(GL_POLYGON_OFFSET_FILL);
                glDisable(GL_CULL_FACE);
                m_shadowPassShader->SetUniform("uShadowPassMode", kProjectedShadowPassMode);

                for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                {
                    auto *cascadeMap = light->shadowCascadeMaps[cascadeIndex].get();
                    if (!cascadeMap)
                    {
                        continue;
                    }

                    const float cascadeNear = cascadeIndex == 0 ? ctx.cameraData.nearPlane : cascadeSplits[cascadeIndex - 1];
                    const float cascadeFar = cascadeSplits[cascadeIndex];
                    const int shadowResolution = cascadeMap->GetWidth() > 0 ? cascadeMap->GetWidth() : GetShadowResolution(*light);
                    const bool cascadeSplitChanged = std::abs(light->shadowCascadeSplits[cascadeIndex] - cascadeFar) > kShadowUpdateMatrixEpsilon;
                    const bool hasStoredCascadeOrigin = !AreMatricesApproximatelyEqual(light->shadowCascadeMatrices[cascadeIndex], glm::mat4(1.0f)) ||
                                                        glm::length(light->shadowCascadeWorldOrigins[cascadeIndex]) > kShadowUpdateMatrixEpsilon;
                    const bool cascadeOriginChanged = ShouldRefreshCameraRelativeCascade(
                        currentShadowWorldOrigin,
                        light->shadowCascadeWorldOrigins[cascadeIndex],
                        cascadeNear,
                        cascadeFar);
                    const glm::vec3 cascadeShadowWorldOrigin = (forceFullCascadeUpdate || !hasStoredCascadeOrigin || cascadeOriginChanged)
                                                                   ? currentShadowWorldOrigin
                                                                   : light->shadowCascadeWorldOrigins[cascadeIndex];
                    const bool realtimeCascadeInvalidation = shadowCastersChanged || light->isDirty;

                    // Split radii drive cascade selection in lighting, so keep them current even when
                    // this cascade's shadow map redraw is deferred by the update cadence.
                    light->shadowCascadeSplits[cascadeIndex] = cascadeFar;

                    const bool forceCascadeUpdate = forceFullCascadeUpdate;
                    const bool cascadeMotionInvalidation = motionDrivenCascadeInvalidation;
                    const bool cadenceWantsUpdate = ShouldUpdateDirectionalCascade(ctx.frameSequence, cascadeIndex, forceCascadeUpdate, cascadeMotionInvalidation);
                    if (!forceFullCascadeUpdate && !realtimeCascadeInvalidation && !cascadeOriginChanged && !hasPendingIncrementalRefresh && !cadenceWantsUpdate)
                    {
                        continue;
                    }

                    const auto cascadeProjection = BuildDirectionalCascadeProjection(*light, ctx.cameraData, shadowCasters, cascadeShadowWorldOrigin, cascadeNear, cascadeFar, shadowResolution);
                    const glm::mat4 &cascadeMatrix = cascadeProjection.lightSpaceMatrix;
                    const bool cascadeMatrixChanged = !AreMatricesApproximatelyEqual(cascadeMatrix, light->shadowCascadeMatrices[cascadeIndex]);
                    if (cameraOnlyInvalidation && !cascadeMatrixChanged && !cascadeSplitChanged && !cascadeOriginChanged)
                    {
                        continue;
                    }

                    if (casterOnlyCascadeInvalidation &&
                        !AnyMovedShadowCasterRelevant(
                            shadowCasters,
                            [&](const ShadowCasterEntry &shadowCaster)
                            {
                                return IsMovedCommandRelevantForDirectionalCascade(
                                    shadowCaster,
                                    cascadeProjection.lightViewMatrix,
                                    cascadeShadowWorldOrigin,
                                    cascadeProjection.receiverMin,
                                    cascadeProjection.receiverMax,
                                    cascadeProjection.receiverExtent,
                                    shadowResolution,
                                    light->directionalShadowSettings.minCasterTexelRadius);
                            }))
                    {
                        continue;
                    }

                    if (!forceFullCascadeUpdate && !realtimeCascadeInvalidation && !reserveIncrementalShadowSurfaceUpdate())
                    {
                        deferredShadowRefresh = true;
                        continue;
                    }

                    light->shadowCascadeWorldOrigins[cascadeIndex] = cascadeShadowWorldOrigin;
                    light->shadowCascadeMatrices[cascadeIndex] = cascadeMatrix;

                    glViewport(0, 0, shadowResolution, shadowResolution);
                    m_shadowPassShader->SetUniform("uShadowWorldOrigin", light->shadowCascadeWorldOrigins[cascadeIndex]);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, cascadeMap->GetTextureID(), 0);
                    if (!ValidateShadowFramebuffer("directional cascade shadow"))
                    {
                        continue;
                    }
                    glClear(GL_DEPTH_BUFFER_BIT);
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", cascadeMatrix);
                    const ShadowDrawStats drawStats = DrawShadowCasterBatches(
                        sortedShadowCasters,
                        [&](const ShadowCasterEntry &shadowCaster)
                        {
                            return IsCommandRelevantForDirectionalCascade(
                                shadowCaster,
                                cascadeProjection.lightViewMatrix,
                                cascadeShadowWorldOrigin,
                                cascadeProjection.receiverMin,
                                cascadeProjection.receiverMax,
                                cascadeProjection.receiverExtent,
                                shadowResolution,
                                light->directionalShadowSettings.minCasterTexelRadius);
                        },
                        m_shadowPassShader,
                        m_instanceBuffer,
                        m_instanceCapacity,
                        shadowBatchInstances);
                    if (ctx.renderer)
                    {
                        ctx.renderer->RecordShadowMapUpdate(
                            shadowResolution * shadowResolution,
                            drawStats.submittedInstances,
                            drawStats.submittedBatches,
                            true);
                    }
                }

                for (int cascadeIndex = cascadeCount; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
                {
                    light->shadowCascadeWorldOrigins[cascadeIndex] = glm::vec3(0.0f);
                    light->shadowCascadeMatrices[cascadeIndex] = glm::mat4(1.0f);
                    light->shadowCascadeSplits[cascadeIndex] = light->shadowFarPlane;
                }

                light->isDirty = false;
                light->shadowRefreshPending = deferredShadowRefresh;
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                m_shadowPassShader->SetUniform("uShadowWorldOrigin", glm::vec3(0.0f));
                continue;
            }

            auto *shadowMap = light->shadowMap.get();
            if (!shadowMap)
            {
                continue;
            }

            const glm::mat4 shadowMatrix = BuildSpotShadowMatrix(*light);
            const auto shadowFrustumPlanes = ExtractFrustumPlanes(shadowMatrix);
            bool projectedShadowNeedsIncrementalRefresh = hasPendingIncrementalRefresh;
            if (!light->isDirty && !projectedShadowNeedsIncrementalRefresh)
            {
                projectedShadowNeedsIncrementalRefresh = AnyMovedShadowCasterRelevant(
                    shadowCasters,
                    [&](const ShadowCasterEntry &shadowCaster)
                    {
                        return light->type != scene::LightType::Spot ||
                               IsMovedCommandRelevantForProjectedLight(shadowCaster, shadowFrustumPlanes);
                    });
            }

            if (!light->isDirty)
            {
                if (!projectedShadowNeedsIncrementalRefresh)
                {
                    light->isDirty = false;
                    light->shadowRefreshPending = false;
                    continue;
                }

                if (!shadowCastersChanged && !reserveIncrementalShadowSurfaceUpdate())
                {
                    light->shadowRefreshPending = true;
                    continue;
                }
            }

            light->shadowMatrix = shadowMatrix;
            light->shadowFarPlane = glm::max(light->range, 0.1f);
            glViewport(0, 0, shadowMap->GetWidth(), shadowMap->GetHeight());

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
            glCullFace(GL_FRONT);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap->GetTextureID(), 0);
            if (!ValidateShadowFramebuffer("projected shadow"))
            {
                continue;
            }
            glClear(GL_DEPTH_BUFFER_BIT);
            m_shadowPassShader->SetUniform("uShadowPassMode", kProjectedShadowPassMode);
            m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrix);
            const ShadowDrawStats drawStats = DrawShadowCasterBatches(
                sortedShadowCasters,
                [&](const ShadowCasterEntry &shadowCaster)
                {
                    return light->type != scene::LightType::Spot ||
                           IsCommandRelevantForProjectedLight(shadowCaster, shadowFrustumPlanes);
                },
                m_shadowPassShader,
                m_instanceBuffer,
                m_instanceCapacity,
                shadowBatchInstances);
            if (ctx.renderer)
            {
                ctx.renderer->RecordShadowMapUpdate(
                    shadowMap->GetWidth() * shadowMap->GetHeight(),
                    drawStats.submittedInstances,
                    drawStats.submittedBatches,
                    false);
            }

            light->isDirty = false;
            light->shadowRefreshPending = false;
            glCullFace(GL_BACK);
        }

        m_shadowPassShader->Unbind();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    }
}
