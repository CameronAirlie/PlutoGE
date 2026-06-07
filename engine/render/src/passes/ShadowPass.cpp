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
#include <cmath>
#include <functional>
#include <limits>
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
        return a.material == b.material &&
               a.mesh == b.mesh &&
               a.submeshIndex == b.submeshIndex;
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
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        std::array<glm::vec3, 8> cascadeCorners{};
        const float nearRadius = glm::max(cascadeNear, cameraData.nearPlane);
        const float farRadius = glm::max(cascadeFar, nearRadius + 0.1f);

        for (std::size_t index = 0; index < 4; ++index)
        {
            const glm::vec3 rayDirection = glm::normalize(frustumCorners[index + 4] - cameraPosition);
            cascadeCorners[index] = cameraPosition + rayDirection * nearRadius;
            cascadeCorners[index + 4] = cameraPosition + rayDirection * farRadius;
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

        for (int cascadeIndex = cascadeCount; cascadeIndex < PlutoGE::scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
        {
            splits[cascadeIndex] = farRadius;
        }

        return splits;
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
        std::array<glm::vec3, 8> cascadeCorners{};
        for (std::size_t index = 0; index < worldCascadeCorners.size(); ++index)
        {
            cascadeCorners[index] = worldCascadeCorners[index] - shadowWorldOrigin;
        }
        glm::vec3 frustumCenter(0.0f);
        for (const glm::vec3 &corner : cascadeCorners)
        {
            frustumCenter += corner;
        }
        frustumCenter /= static_cast<float>(cascadeCorners.size());

        float receiverRadius = 0.0f;
        for (const glm::vec3 &corner : cascadeCorners)
        {
            receiverRadius = glm::max(receiverRadius, glm::length(corner - frustumCenter));
        }

        receiverRadius = glm::max(receiverRadius + kDirectionalShadowPadding, 10.0f);
        const float casterExtrusionDistance = receiverRadius * 2.0f + kDirectionalShadowPadding;
        const glm::vec3 upVector = ResolveUpVector(lightDirection);
        const glm::mat4 rotationOnlyView = glm::lookAt(glm::vec3(0.0f), lightDirection, upVector);
        const glm::vec3 lightSpaceCenter = glm::vec3(rotationOnlyView * glm::vec4(frustumCenter, 1.0f));

        const glm::vec2 extents(receiverRadius * 2.0f);
        const glm::vec2 texelSize = extents / static_cast<float>(shadowResolution);
        const glm::vec2 snappedCenterXY = glm::round(glm::vec2(lightSpaceCenter) / texelSize) * texelSize;
        const float pcfGuardTexels = glm::max(light.directionalShadowSettings.softness + 1.0f, 2.0f);
        const glm::vec2 halfExtents = extents * 0.5f + texelSize * pcfGuardTexels;
        const glm::vec3 lightRight = glm::normalize(glm::cross(lightDirection, upVector));
        const glm::vec3 lightUp = glm::normalize(glm::cross(lightRight, lightDirection));

        glm::vec3 eye = frustumCenter +
                        lightRight * (lightSpaceCenter.x - snappedCenterXY.x) +
                        lightUp * (lightSpaceCenter.y - snappedCenterXY.y);

        auto computeCascadeBounds = [&](const glm::mat4 &view, glm::vec3 &minBounds, glm::vec3 &maxBounds)
        {
            const glm::vec3 centeredLightSpace = glm::vec3(view * glm::vec4(frustumCenter, 1.0f));
            minBounds = glm::vec3(centeredLightSpace.x - halfExtents.x, centeredLightSpace.y - halfExtents.y, std::numeric_limits<float>::max());
            maxBounds = glm::vec3(centeredLightSpace.x + halfExtents.x, centeredLightSpace.y + halfExtents.y, std::numeric_limits<float>::lowest());

            for (const glm::vec3 &corner : cascadeCorners)
            {
                const glm::vec3 lightSpaceCorner = glm::vec3(view * glm::vec4(corner, 1.0f));
                minBounds.z = glm::min(minBounds.z, lightSpaceCorner.z);
                maxBounds.z = glm::max(maxBounds.z, lightSpaceCorner.z);

                const glm::vec3 extrudedCorner = corner - lightDirection * casterExtrusionDistance;
                const glm::vec3 lightSpaceExtrudedCorner = glm::vec3(view * glm::vec4(extrudedCorner, 1.0f));
                minBounds.z = glm::min(minBounds.z, lightSpaceExtrudedCorner.z);
                maxBounds.z = glm::max(maxBounds.z, lightSpaceExtrudedCorner.z);
            }

            minBounds -= glm::vec3(0.0f, 0.0f, kDirectionalShadowPadding);
            maxBounds += glm::vec3(0.0f, 0.0f, kDirectionalShadowPadding);
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
        const glm::mat4 projection = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, nearPlane, farPlane);
        return DirectionalCascadeProjection{
            .lightSpaceMatrix = projection * view,
            .lightViewMatrix = view,
            .receiverMin = glm::vec2(minBounds.x, minBounds.y),
            .receiverMax = glm::vec2(maxBounds.x, maxBounds.y),
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
                                               const glm::vec2 &receiverMax)
    {
        const glm::vec3 relativeCenter = bounds.center - shadowWorldOrigin;
        const glm::vec3 lightSpaceCenter = glm::vec3(lightView * glm::vec4(relativeCenter, 1.0f));
        const float radius = glm::max(bounds.radius, 0.001f);

        return !(lightSpaceCenter.x + radius < receiverMin.x ||
                 lightSpaceCenter.x - radius > receiverMax.x ||
                 lightSpaceCenter.y + radius < receiverMin.y ||
                 lightSpaceCenter.y - radius > receiverMax.y);
    }

    bool IsCommandRelevantForDirectionalCascade(const ShadowCasterEntry &shadowCaster,
                                                const glm::mat4 &lightView,
                                                const glm::vec3 &shadowWorldOrigin,
                                                const glm::vec2 &receiverMin,
                                                const glm::vec2 &receiverMax)
    {
        return IsBoundsRelevantForDirectionalCascade(shadowCaster.bounds, lightView, shadowWorldOrigin, receiverMin, receiverMax);
    }

    bool IsMovedCommandRelevantForDirectionalCascade(const ShadowCasterEntry &shadowCaster,
                                                    const glm::mat4 &lightView,
                                                    const glm::vec3 &shadowWorldOrigin,
                                                    const glm::vec2 &receiverMin,
                                                    const glm::vec2 &receiverMax)
    {
        return shadowCaster.hasMoved &&
               (IsBoundsRelevantForDirectionalCascade(shadowCaster.bounds, lightView, shadowWorldOrigin, receiverMin, receiverMax) ||
                IsBoundsRelevantForDirectionalCascade(shadowCaster.previousBounds, lightView, shadowWorldOrigin, receiverMin, receiverMax));
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
        if (albedoTexture)
        {
            shader->SetUniform("uAlbedoTexture", albedoTexture, 0);
            shader->SetUniform("uHasAlbedoTexture", 1.0f);
            return;
        }

        shader->SetUniform("uHasAlbedoTexture", 0.0f);
    }

    template <typename Predicate>
    void DrawShadowCasterBatches(const std::vector<ShadowCasterEntry> &shadowCasters,
                                 Predicate &&predicate,
                                 PlutoGE::render::Shader *shader,
                                 unsigned int &instanceBuffer,
                                 std::size_t &instanceCapacity,
                                 std::vector<const PlutoGE::render::RenderCommand *> &visibleCommands,
                                 std::vector<TransformInstanceData> &batchInstances)
    {
        PlutoGE::render::Material *boundMaterial = nullptr;
        PlutoGE::render::Mesh *boundMesh = nullptr;
        visibleCommands.clear();
        batchInstances.clear();
        if (visibleCommands.capacity() < shadowCasters.size())
        {
            visibleCommands.reserve(shadowCasters.size());
        }
        if (batchInstances.capacity() < 64)
        {
            batchInstances.reserve(64);
        }

        for (const auto &shadowCaster : shadowCasters)
        {
            if (predicate(shadowCaster))
            {
                visibleCommands.push_back(shadowCaster.command);
            }
        }

        std::sort(visibleCommands.begin(), visibleCommands.end(), [](const auto *a, const auto *b)
                  {
                      if (a->material != b->material)
                      {
                          return std::less<PlutoGE::render::Material *>{}(a->material, b->material);
                      }
                      if (a->mesh != b->mesh)
                      {
                          return std::less<PlutoGE::render::Mesh *>{}(a->mesh, b->mesh);
                      }
                      return a->submeshIndex < b->submeshIndex;
                  });

        const auto flushBatch = [&](const PlutoGE::render::RenderCommand &batchHead)
        {
            if (batchInstances.empty())
            {
                return;
            }

            if (batchHead.material != boundMaterial)
            {
                BindShadowMaterialState(shader, batchHead.material);
                boundMaterial = batchHead.material;
            }

            UploadTransformInstances(instanceBuffer, instanceCapacity, batchInstances);
            if (batchHead.mesh != boundMesh)
            {
                BindTransformInstanceAttributes(*batchHead.mesh, instanceBuffer);
                boundMesh = batchHead.mesh;
            }

            batchHead.mesh->DrawSubmeshInstancedBound(batchHead.submeshIndex, batchInstances.size());
            batchInstances.clear();
        };

        const PlutoGE::render::RenderCommand *batchHead = nullptr;
        for (const auto *command : visibleCommands)
        {
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
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_POLYGON_OFFSET_FILL);

        m_shadowPassShader->Bind();
        m_shadowPassShader->SetUniform("uShadowWorldOrigin", glm::vec3(0.0f));
        std::vector<ShadowCasterEntry> shadowCasters;
        bool shadowCastersChanged = false;
        BuildShadowCasterEntries(*ctx.renderCommands, shadowCasters, shadowCastersChanged);
        const bool cameraDataChanged = ctx.hasCameraData && (!ctx.hasPreviousCameraData || HasCameraDataChanged(ctx.cameraData, ctx.previousCameraData));
        std::vector<const RenderCommand *> visibleShadowCommands;
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

                        if (!reserveIncrementalShadowSurfaceUpdate())
                        {
                            deferredShadowRefresh = true;
                            continue;
                        }
                    }

                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, shadowMap->GetTextureID(), 0);
                    glClear(GL_DEPTH_BUFFER_BIT);
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrices[face]);
                    DrawShadowCasterBatches(
                        shadowCasters,
                        [&](const ShadowCasterEntry &shadowCaster)
                        {
                            return IsCommandRelevantForPointLight(shadowCaster, *light) &&
                                   IsCommandRelevantForProjectedLight(shadowCaster, faceFrustumPlanes);
                        },
                        m_shadowPassShader,
                        m_instanceBuffer,
                        m_instanceCapacity,
                        visibleShadowCommands,
                        shadowBatchInstances);
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
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 2.0f);
                glCullFace(GL_BACK);
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
                    const bool cascadeOriginChanged = ShouldRefreshCameraRelativeCascade(
                        currentShadowWorldOrigin,
                        light->shadowCascadeWorldOrigins[cascadeIndex],
                        cascadeNear,
                        cascadeFar);

                    // Split radii drive cascade selection in lighting, so keep them current even when
                    // this cascade's shadow map redraw is deferred by the update cadence.
                    light->shadowCascadeSplits[cascadeIndex] = cascadeFar;

                    const bool forceCascadeUpdate = forceFullCascadeUpdate;
                    const bool cascadeMotionInvalidation = motionDrivenCascadeInvalidation;
                    const bool cadenceWantsUpdate = ShouldUpdateDirectionalCascade(ctx.frameSequence, cascadeIndex, forceCascadeUpdate, cascadeMotionInvalidation);
                    if (!forceFullCascadeUpdate && !cascadeOriginChanged && !hasPendingIncrementalRefresh && !cadenceWantsUpdate)
                    {
                        continue;
                    }

                    const auto cascadeProjection = BuildDirectionalCascadeProjection(*light, ctx.cameraData, shadowCasters, currentShadowWorldOrigin, cascadeNear, cascadeFar, shadowResolution);
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
                                    light->shadowCascadeWorldOrigins[cascadeIndex],
                                    cascadeProjection.receiverMin,
                                    cascadeProjection.receiverMax);
                            }))
                    {
                        continue;
                    }

                    if (!forceFullCascadeUpdate && !reserveIncrementalShadowSurfaceUpdate())
                    {
                        deferredShadowRefresh = true;
                        continue;
                    }

                    light->shadowCascadeWorldOrigins[cascadeIndex] = currentShadowWorldOrigin;
                    light->shadowCascadeMatrices[cascadeIndex] = cascadeMatrix;

                    glViewport(0, 0, shadowResolution, shadowResolution);
                    m_shadowPassShader->SetUniform("uShadowWorldOrigin", light->shadowCascadeWorldOrigins[cascadeIndex]);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, cascadeMap->GetTextureID(), 0);
                    glClear(GL_DEPTH_BUFFER_BIT);
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", cascadeMatrix);
                    DrawShadowCasterBatches(
                        shadowCasters,
                        [&](const ShadowCasterEntry &shadowCaster)
                        {
                            return IsCommandRelevantForDirectionalCascade(
                                shadowCaster,
                                cascadeProjection.lightViewMatrix,
                                light->shadowCascadeWorldOrigins[cascadeIndex],
                                cascadeProjection.receiverMin,
                                cascadeProjection.receiverMax);
                        },
                        m_shadowPassShader,
                        m_instanceBuffer,
                        m_instanceCapacity,
                        visibleShadowCommands,
                        shadowBatchInstances);
                }

                for (int cascadeIndex = cascadeCount; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
                {
                    light->shadowCascadeWorldOrigins[cascadeIndex] = glm::vec3(0.0f);
                    light->shadowCascadeMatrices[cascadeIndex] = glm::mat4(1.0f);
                    light->shadowCascadeSplits[cascadeIndex] = light->shadowFarPlane;
                }

                light->isDirty = false;
                light->shadowRefreshPending = deferredShadowRefresh;
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

                if (!reserveIncrementalShadowSurfaceUpdate())
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
            glClear(GL_DEPTH_BUFFER_BIT);
            m_shadowPassShader->SetUniform("uShadowPassMode", kProjectedShadowPassMode);
            m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrix);
            DrawShadowCasterBatches(
                shadowCasters,
                [&](const ShadowCasterEntry &shadowCaster)
                {
                    return light->type != scene::LightType::Spot ||
                           IsCommandRelevantForProjectedLight(shadowCaster, shadowFrustumPlanes);
                },
                m_shadowPassShader,
                m_instanceBuffer,
                m_instanceCapacity,
                visibleShadowCommands,
                shadowBatchInstances);

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
