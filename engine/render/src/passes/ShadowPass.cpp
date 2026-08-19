#include "PlutoGE/render/passes/ShadowPass.h"
#include "PlutoGE/render/Graphics.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/IndirectDraw.h"
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
#include <numeric>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace
{
    constexpr int kProjectedShadowPassMode = 0;
    constexpr int kPointShadowPassMode = 1;
    // Reserve one update for the camera/player-facing near cascade and one for
    // queued background work. Updating only a single weighted cascade made the
    // near map advance every few frames while walking or looking around, which
    // was visible as flicker on first-person weapons and stepped ground shadows.
    constexpr int kMaxIncrementalShadowSurfaceUpdatesPerFrame = 2;
    constexpr std::uint8_t kAllPointShadowFacesMask = 0x3fu;
    constexpr float kDirectionalShadowDepthPadding = 2.0f;
    constexpr float kShadowUpdateMatrixEpsilon = 0.0001f;
    constexpr float kNearCascadeMinCasterTexelRadius = 0.35f;
    constexpr float kFarCascadeMinCasterTexelRadius = 1.0f;
    constexpr float kDirectionalShadowSlopeBias = 1.5f;
    constexpr float kDirectionalShadowConstantBias = 2.0f;
    constexpr std::array<int, 8> kDirectionalCascadeRefreshSchedule{0, 1, 0, 2, 0, 1, 0, 3};

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

    struct DirectionalShadowDirtyRegion
    {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};
        int pixelX = 0;
        int pixelY = 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
        bool valid = false;
    };

    struct DirectionalShadowScroll
    {
        int x = 0;
        int y = 0;
        float maxMatrixDelta = 0.0f;
        float fractionalTexelError = 0.0f;
        glm::mat4 resolvedRelativeMatrix{1.0f};
        bool valid = false;
    };

    struct ShadowDrawStats
    {
        int submittedInstances = 0;
        int submittedBatches = 0;
        int submittedTriangles = 0;
        int materialGroups = 0;
        int apiDrawCalls = 0;
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

    bool HasDirectionalCameraOrientationOrProjectionChanged(const PlutoGE::render::CameraData &current,
                                                            const PlutoGE::render::CameraData &previous)
    {
        if (!AreMatricesApproximatelyEqual(current.projection, previous.projection) ||
            std::abs(current.nearPlane - previous.nearPlane) > kShadowUpdateMatrixEpsilon ||
            std::abs(current.farPlane - previous.farPlane) > kShadowUpdateMatrixEpsilon)
        {
            return true;
        }

        // A view matrix's upper-left 3x3 contains camera orientation. Ignore its
        // translation here: directional cascades have their own distance-based
        // recenter thresholds, and treating every sub-texel camera movement as a
        // camera invalidation continuously requeues all cascades.
        for (int column = 0; column < 3; ++column)
        {
            for (int row = 0; row < 3; ++row)
            {
                if (std::abs(current.view[column][row] - previous.view[column][row]) > kShadowUpdateMatrixEpsilon)
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool ShouldRefreshCameraRelativeCascade(const glm::vec3 &currentOrigin,
                                            const glm::vec3 &storedOrigin,
                                            float cascadeNear,
                                            float cascadeFar,
                                            int shadowResolution)
    {
        const float cascadeThickness = glm::max(cascadeFar - cascadeNear, 0.1f);
        // Refresh when camera translation reaches approximately one shadow
        // texel. Waiting for a fraction of the entire cascade made the cached
        // projection jump by metres at once, which looked like stationary
        // shadows moving whenever a sprint crossed the threshold.
        const float recenterThreshold = glm::max(
            cascadeThickness / static_cast<float>(std::max(shadowResolution, 1)),
            0.0025f);
        return glm::distance(currentOrigin, storedOrigin) > recenterThreshold;
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

    void BindTransformInstanceAttributes(const PlutoGE::render::Mesh &mesh,
                                         unsigned int instanceBuffer,
                                         std::size_t firstInstance)
    {
        const std::size_t baseOffset = firstInstance * sizeof(TransformInstanceData);
        glBindVertexArray(mesh.GetVAO());
        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        ConfigureMatrixAttributes(5, baseOffset + offsetof(TransformInstanceData, model), sizeof(TransformInstanceData));
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
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instanceCapacity * sizeof(TransformInstanceData)), nullptr, GL_STREAM_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instances.size() * sizeof(TransformInstanceData)), instances.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    bool CanBatchShadowCommands(const PlutoGE::render::RenderCommand &a,
                                const PlutoGE::render::RenderCommand &b,
                                std::size_t aLodIndex,
                                std::size_t bLodIndex)
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
               a.mesh->GetSubmeshLodRange(a.submeshIndex, aLodIndex).indexOffset == b.mesh->GetSubmeshLodRange(b.submeshIndex, bLodIndex).indexOffset &&
               a.mesh->GetSubmeshLodRange(a.submeshIndex, aLodIndex).indexCount == b.mesh->GetSubmeshLodRange(b.submeshIndex, bLodIndex).indexCount;
    }

    std::size_t ClampShadowLodIndex(const PlutoGE::render::RenderCommand &command, std::size_t lodIndex)
    {
        if (!command.mesh)
        {
            return lodIndex;
        }

        const std::size_t lodCount = command.mesh->GetSubmeshLodCount(command.submeshIndex);
        return lodCount > 0 ? std::min(lodIndex, lodCount - 1) : 0;
    }

    std::size_t SelectDefaultShadowLod(const PlutoGE::render::RenderCommand &command)
    {
        return ClampShadowLodIndex(command, std::max<std::size_t>(command.lodIndex, command.minShadowLodIndex));
    }

    std::size_t SelectDirectionalShadowLod(const PlutoGE::render::RenderCommand &command,
                                           int /*cascadeIndex*/,
                                           int /*cascadeCount*/,
                                           int shadowResolution)
    {
        // Never make a shadow draw more detailed than the LOD already selected
        // for visible scene geometry. All cascades must use the same caster LOD,
        // otherwise the silhouette changes at cascade boundaries and stationary
        // shadows appear to wiggle as the camera moves across those boundaries.
        const std::size_t sceneLod = command.lodIndex;
        const std::size_t resolutionLodFloor = shadowResolution <= 1024 ? 1u : 0u;
        const std::size_t automaticShadowLod = resolutionLodFloor;
        return ClampShadowLodIndex(command, std::max<std::size_t>(
                                                std::max<std::size_t>(sceneLod, automaticShadowLod),
                                                command.minShadowLodIndex));
    }

    bool IsAlphaTestedShadowCaster(const PlutoGE::render::RenderCommand &command)
    {
        return command.material && command.material->GetConfig().albedoTexture && command.material->GetConfig().alphaMode == PlutoGE::render::AlphaMode::Mask;
    }

    bool CastsShadow(const PlutoGE::render::RenderCommand &command)
    {
        return command.material &&
               command.castsShadow &&
               command.material->GetConfig().castsShadow &&
               command.material->GetConfig().alphaMode != PlutoGE::render::AlphaMode::Blend;
    }

    void HashCombine(std::uint64_t &seed, std::uint64_t value)
    {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    }

    bool ValidateShadowFramebuffer(const char *label, unsigned int attachmentTexture)
    {
        // Shadow targets persist across frames. glCheckFramebufferStatus can
        // synchronize with the driver, so validate each newly created texture
        // once rather than once per dynamic cascade redraw.
        static std::unordered_set<unsigned int> validatedAttachments;
        if (validatedAttachments.contains(attachmentTexture))
            return true;
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status == GL_FRAMEBUFFER_COMPLETE)
        {
            validatedAttachments.insert(attachmentTexture);
            return true;
        }

        std::cerr << "Shadow framebuffer incomplete for " << label << ": 0x" << std::hex << status << std::dec << std::endl;
        return false;
    }

    void BuildShadowCasterEntries(const std::vector<PlutoGE::render::RenderCommand> &renderCommands,
                                  const std::vector<std::size_t> &sortedCommandIndices,
                                  std::vector<ShadowCasterEntry> &shadowCasters,
                                  bool &shadowCastersChanged)
    {
        shadowCasters.clear();
        shadowCasters.reserve(sortedCommandIndices.size());
        shadowCastersChanged = false;

        for (const std::size_t commandIndex : sortedCommandIndices)
        {
            if (commandIndex >= renderCommands.size())
                continue;
            const auto &command = renderCommands[commandIndex];

            // Skeletal animation changes vertex positions without changing the
            // model matrix. Treat a newly evaluated skinning pose as caster
            // motion so cached shadow maps redraw with the current bones.
            const bool hasMoved = command.skinningPoseChanged ||
                                  !AreMatricesApproximatelyEqual(command.model, command.previousModel);
            shadowCastersChanged = shadowCastersChanged || hasMoved;
            shadowCasters.push_back(ShadowCasterEntry{
                .command = &command,
                .bounds = command.worldBounds,
                .previousBounds = command.previousWorldBounds,
                .hasMoved = hasMoved,
            });
        }
    }

    std::vector<std::size_t> BuildSortedShadowCasterCommandIndices(
        const std::vector<PlutoGE::render::RenderCommand> &renderCommands)
    {
        std::vector<std::size_t> indices;
        indices.reserve(renderCommands.size());
        for (std::size_t index = 0; index < renderCommands.size(); ++index)
        {
            if (renderCommands[index].mesh && renderCommands[index].material && CastsShadow(renderCommands[index]))
                indices.push_back(index);
        }

        std::sort(indices.begin(), indices.end(), [&](std::size_t aIndex, std::size_t bIndex)
                  {
            const auto *aCommand = &renderCommands[aIndex];
            const auto *bCommand = &renderCommands[bIndex];
            const bool aAlphaTested = IsAlphaTestedShadowCaster(*aCommand);
            const bool bAlphaTested = IsAlphaTestedShadowCaster(*bCommand);
            if (aAlphaTested != bAlphaTested) return aAlphaTested < bAlphaTested;
            if (aAlphaTested && aCommand->material != bCommand->material)
                return std::less<PlutoGE::render::Material *>{}(aCommand->material, bCommand->material);
            if (aCommand->mesh != bCommand->mesh)
                return std::less<PlutoGE::render::Mesh *>{}(aCommand->mesh, bCommand->mesh);
            if (aCommand->jointMatrices != bCommand->jointMatrices)
                return std::less<const std::vector<glm::mat4> *>{}(aCommand->jointMatrices, bCommand->jointMatrices);
            if (aCommand->submeshIndex != bCommand->submeshIndex)
                return aCommand->submeshIndex < bCommand->submeshIndex;
            return aCommand->lodIndex < bCommand->lodIndex; });
        return indices;
    }

    struct ShadowCasterFrameState
    {
        std::uint64_t fingerprint = 1469598103934665603ull;
        std::size_t casterCount = 0;
        bool hasMovedCaster = false;
        bool allCastersStatic = true;
    };

    ShadowCasterFrameState InspectShadowCasters(const std::vector<PlutoGE::render::RenderCommand> &renderCommands)
    {
        ShadowCasterFrameState state;
        for (const auto &command : renderCommands)
        {
            if (!command.mesh || !command.material || !CastsShadow(command))
            {
                continue;
            }

            ++state.casterCount;
            state.allCastersStatic = state.allCastersStatic && command.isStatic && command.jointMatrices == nullptr;
            state.hasMovedCaster = state.hasMovedCaster ||
                                   command.skinningPoseChanged ||
                                   !AreMatricesApproximatelyEqual(command.model, command.previousModel);
            HashCombine(state.fingerprint, reinterpret_cast<std::uintptr_t>(command.mesh));
            HashCombine(state.fingerprint, reinterpret_cast<std::uintptr_t>(command.material));
            HashCombine(state.fingerprint, static_cast<std::uint64_t>(command.submeshIndex));
            HashCombine(state.fingerprint, static_cast<std::uint64_t>(command.lodIndex));
            // Bounds are transform state, not topology. Including them here made
            // every moving caster look like a structural scene change and forced
            // complete cascade redraws, bypassing both scrolling and dirty-region
            // updates. Motion is tracked separately through previous transforms
            // and skinningPoseChanged above.
        }

        HashCombine(state.fingerprint, static_cast<std::uint64_t>(state.casterCount));
        return state;
    }

    std::array<glm::vec3, 8> BuildCameraRelativeFrustumCorners(const PlutoGE::render::CameraData &cameraData)
    {
        const glm::mat4 inverseProjection = glm::inverse(cameraData.projection);
        const glm::mat3 inverseViewRotation = glm::mat3(glm::inverse(cameraData.view));
        std::array<glm::vec3, 8> corners{};
        std::size_t index = 0;

        for (int z = 0; z < 2; ++z)
        {
            // The scene camera uses reversed Z: clip-space +1 is the near plane
            // and -1 is the far plane. Keep the returned corners ordered as
            // near first, far second for cascade slicing below.
            const float clipZ = z == 0 ? 1.0f : -1.0f;
            for (int y = 0; y < 2; ++y)
            {
                const float clipY = y == 0 ? -1.0f : 1.0f;
                for (int x = 0; x < 2; ++x)
                {
                    const float clipX = x == 0 ? -1.0f : 1.0f;
                    const glm::vec4 viewCorner = inverseProjection * glm::vec4(clipX, clipY, clipZ, 1.0f);
                    corners[index++] = inverseViewRotation * (glm::vec3(viewCorner) / viewCorner.w);
                }
            }
        }

        return corners;
    }

    std::array<glm::vec3, 8> BuildCameraRelativeCascadeFrustumCorners(const PlutoGE::render::CameraData &cameraData, float cascadeNear, float cascadeFar)
    {
        const auto frustumCorners = BuildCameraRelativeFrustumCorners(cameraData);
        std::array<glm::vec3, 8> cascadeCorners{};
        const float nearDepth = glm::max(cascadeNear, cameraData.nearPlane);
        const float farDepth = glm::max(cascadeFar, nearDepth + 0.1f);
        const float cameraDepthRange = glm::max(cameraData.farPlane - cameraData.nearPlane, 0.0001f);
        const float nearFactor = glm::clamp((nearDepth - cameraData.nearPlane) / cameraDepthRange, 0.0f, 1.0f);
        const float farFactor = glm::clamp((farDepth - cameraData.nearPlane) / cameraDepthRange, 0.0f, 1.0f);

        for (std::size_t index = 0; index < 4; ++index)
        {
            const glm::vec3 &nearCorner = frustumCorners[index];
            const glm::vec3 &farCorner = frustumCorners[index + 4];
            cascadeCorners[index] = nearCorner + (farCorner - nearCorner) * nearFactor;
            cascadeCorners[index + 4] = nearCorner + (farCorner - nearCorner) * farFactor;
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

    std::uint8_t BuildMovedCasterCascadeMask(
        const std::vector<ShadowCasterEntry> &shadowCasters,
        const PlutoGE::render::CameraData &cameraData,
        const std::array<float, PlutoGE::scene::kMaxDirectionalShadowCascades> &cascadeSplits,
        int cascadeCount,
        float cascadeBlendDistance)
    {
        std::uint8_t mask = 0;
        const float transitionMargin = glm::max(cascadeBlendDistance, 0.0f);

        auto includeBounds = [&](const PlutoGE::render::MeshBounds &bounds)
        {
            const float viewDepth = -(cameraData.view * glm::vec4(bounds.center, 1.0f)).z;
            const float depthRadius = glm::max(bounds.radius, 0.001f) + transitionMargin;
            for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
            {
                const float cascadeNear = cascadeIndex == 0 ? cameraData.nearPlane : cascadeSplits[cascadeIndex - 1];
                const float cascadeFar = cascadeSplits[cascadeIndex];
                if (viewDepth + depthRadius >= cascadeNear &&
                    viewDepth - depthRadius <= cascadeFar)
                {
                    mask |= static_cast<std::uint8_t>(1u << cascadeIndex);
                }
            }
        };

        for (const ShadowCasterEntry &shadowCaster : shadowCasters)
        {
            if (!shadowCaster.hasMoved)
            {
                continue;
            }

            // Include both positions so the old shadow is erased immediately
            // and the new one is drawn in the same frame.
            includeBounds(shadowCaster.previousBounds);
            includeBounds(shadowCaster.bounds);
        }

        return mask;
    }

    void SnapDirectionalProjectionBoundsToWorldTexels(glm::vec3 &minBounds,
                                                      glm::vec3 &maxBounds,
                                                      const glm::mat4 &view,
                                                      const glm::vec3 &shadowWorldOrigin,
                                                      int shadowResolution)
    {
        const float safeResolution = static_cast<float>(std::max(shadowResolution, 1));
        const glm::vec2 extents = glm::max(glm::vec2(maxBounds.x - minBounds.x, maxBounds.y - minBounds.y), glm::vec2(0.001f));
        const glm::vec2 texelSize = extents / safeResolution;
        // Shadow matrices consume positions relative to shadowWorldOrigin. Anchor
        // the projection to absolute world zero in light space so camera motion
        // cannot change the shadow texel-grid phase. Adjusting the bounds keeps
        // dirty-region calculations and the projection on the same grid.
        const glm::vec2 worldAnchor = glm::vec2(view * glm::vec4(-shadowWorldOrigin, 1.0f));
        const glm::vec2 minimum(minBounds.x, minBounds.y);
        const glm::vec2 anchorTexel = (worldAnchor - minimum) / texelSize;
        const glm::vec2 snappedMinimum = worldAnchor - glm::round(anchorTexel) * texelSize;
        const glm::vec2 offset = snappedMinimum - minimum;

        minBounds.x += offset.x;
        maxBounds.x += offset.x;
        minBounds.y += offset.y;
        maxBounds.y += offset.y;
    }

    DirectionalCascadeProjection BuildDirectionalCascadeProjection(
        const PlutoGE::scene::Light &light,
        const PlutoGE::render::CameraData &cameraData,
        const glm::vec3 &shadowWorldOrigin,
        float cascadeNear,
        float cascadeFar,
        int shadowResolution)
    {
        const glm::vec3 lightDirection = glm::normalize(light.direction);
        // Gameplay cameras commonly animate FOV while sprinting or aiming. Keep
        // farther cascades conservative for stability, but let cascade zero use
        // the actual camera projection so close receivers get the intended
        // world-space texel density.
        PlutoGE::render::CameraData shadowCameraData = cameraData;
        constexpr float kStableProjectionY = 1.0f; // 1 / tan(90 degrees / 2)
        const float projectionY = std::abs(shadowCameraData.projection[1][1]);
        const float projectionX = std::abs(shadowCameraData.projection[0][0]);
        const bool isNearCascade = cascadeNear <= cameraData.nearPlane + kShadowUpdateMatrixEpsilon;
        if (!isNearCascade && projectionY > kStableProjectionY && projectionX > 0.000001f)
        {
            const float aspectRatio = projectionY / projectionX;
            shadowCameraData.projection[1][1] = std::copysign(kStableProjectionY, shadowCameraData.projection[1][1]);
            shadowCameraData.projection[0][0] = std::copysign(kStableProjectionY / aspectRatio, shadowCameraData.projection[0][0]);
        }
        const auto cameraRelativeCascadeCorners = BuildCameraRelativeCascadeFrustumCorners(shadowCameraData, cascadeNear, cascadeFar);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        glm::vec3 cameraRelativeCascadeCenter(0.0f);
        for (const glm::vec3 &corner : cameraRelativeCascadeCorners)
        {
            cameraRelativeCascadeCenter += corner;
        }
        cameraRelativeCascadeCenter /= static_cast<float>(cameraRelativeCascadeCorners.size());

        // Fit the stable sphere around this cascade slice rather than around
        // the camera. Camera-centred cascades waste texels behind each slice.
        const glm::vec3 cascadeCenter = cameraPosition + cameraRelativeCascadeCenter - shadowWorldOrigin;
        float cascadeRadius = 0.0f;
        for (const glm::vec3 &corner : cameraRelativeCascadeCorners)
        {
            cascadeRadius = glm::max(cascadeRadius, glm::length(corner - cameraRelativeCascadeCenter));
        }
        // A quantized radius and texel-snapped centre keep the projection
        // stable as the camera moves and rotates.
        cascadeRadius = glm::ceil(glm::max(cascadeRadius, 0.1f) * 16.0f) / 16.0f;

        const float cascadeDepth = glm::max(cascadeFar - cascadeNear, 1.0f);
        const float casterExtrusionDistance = cascadeDepth * 2.0f + kDirectionalShadowDepthPadding;
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
            const glm::vec2 xyGuard = texelSize * pcfGuardTexels;
            minBounds -= glm::vec3(xyGuard.x, xyGuard.y, kDirectionalShadowDepthPadding);
            maxBounds += glm::vec3(xyGuard.x, xyGuard.y, kDirectionalShadowDepthPadding);
        };

        glm::mat4 view = glm::lookAt(eye, eye + lightDirection, upVector);
        glm::vec3 minBounds(0.0f);
        glm::vec3 maxBounds(0.0f);
        computeCascadeBounds(view, minBounds, maxBounds);

        if (maxBounds.z > -kDirectionalShadowDepthPadding)
        {
            const float retreatDistance = maxBounds.z + kDirectionalShadowDepthPadding;
            eye -= lightDirection * retreatDistance;
            view = glm::lookAt(eye, eye + lightDirection, upVector);
            computeCascadeBounds(view, minBounds, maxBounds);
        }

        const float nearPlane = glm::max(0.1f, -maxBounds.z);
        const float farPlane = glm::max(nearPlane + 0.1f, -minBounds.z);
        SnapDirectionalProjectionBoundsToWorldTexels(
            minBounds, maxBounds, view, shadowWorldOrigin, shadowResolution);
        glm::mat4 projection = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, nearPlane, farPlane);
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

    DirectionalShadowScroll ResolveDirectionalShadowScroll(const glm::mat4 &previousRelative,
                                                           const glm::vec3 &previousWorldOrigin,
                                                           const glm::mat4 &currentRelative,
                                                           const glm::vec3 &currentWorldOrigin,
                                                           int resolution)
    {
        DirectionalShadowScroll result;
        result.resolvedRelativeMatrix = currentRelative;
        if (resolution <= 0 || AreMatricesApproximatelyEqual(previousRelative, glm::mat4(1.0f)))
        {
            return result;
        }

        // Cascade matrices consume positions relative to a camera-local world
        // origin. Compose that origin back into each transform before comparing
        // cached mappings; otherwise a harmless origin recenter appears as an
        // unrelated XY/Z matrix change and rejects scrolling every time.
        const glm::mat4 previous = previousRelative * glm::translate(glm::mat4(1.0f), -previousWorldOrigin);
        glm::mat4 current = currentRelative * glm::translate(glm::mat4(1.0f), -currentWorldOrigin);

        // Cached depth can be translated only when the world-to-depth mapping
        // is otherwise identical. Any rotation, scale, or depth-range change
        // falls back to a complete cascade refresh.
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (column == 3 && (row == 0 || row == 1 || row == 2))
                {
                    continue;
                }
                result.maxMatrixDelta = glm::max(
                    result.maxMatrixDelta,
                    std::abs(previous[column][row] - current[column][row]));
            }
        }

        if (result.maxMatrixDelta > kShadowUpdateMatrixEpsilon)
            return result;

        // Fitted cascades move slightly along the light's depth axis as the FPS
        // camera moves or rotates. Cached depth remains exact if rendering and
        // sampling retain the previous absolute Z mapping. Bound the retained
        // range so a periodic full refresh recentres depth coverage safely.
        const float depthTranslationDelta = std::abs(previous[3][2] - current[3][2]);
        result.maxMatrixDelta = glm::max(result.maxMatrixDelta, depthTranslationDelta);
        constexpr float kMaximumRetainedDepthTranslation = 0.02f;
        if (depthTranslationDelta > kMaximumRetainedDepthTranslation)
            return result;
        current[3][2] = previous[3][2];
        result.resolvedRelativeMatrix = current *
                                        glm::translate(glm::mat4(1.0f), currentWorldOrigin);

        const glm::vec2 pixelDelta = glm::vec2(current[3] - previous[3]) *
                                     (static_cast<float>(resolution) * 0.5f);
        const glm::ivec2 rounded = glm::ivec2(glm::round(pixelDelta));
        const glm::vec2 fractionalError = glm::abs(pixelDelta - glm::vec2(rounded));
        result.fractionalTexelError = glm::max(fractionalError.x, fractionalError.y);
        if (result.fractionalTexelError > 0.05f ||
            std::abs(rounded.x) >= resolution || std::abs(rounded.y) >= resolution)
        {
            return result;
        }
        result.x = rounded.x;
        result.y = rounded.y;
        // A zero offset is still a successful cache reuse. It means only moving
        // caster dirty regions need redrawing; no texture scroll is necessary.
        result.valid = true;
        return result;
    }

    bool IsBoundsOverlappingDirectionalRegion(const PlutoGE::render::MeshBounds &bounds,
                                              const glm::mat4 &lightView,
                                              const glm::vec3 &shadowWorldOrigin,
                                              const glm::vec2 &regionMin,
                                              const glm::vec2 &regionMax)
    {
        const glm::vec3 relativeCenter = bounds.center - shadowWorldOrigin;
        const glm::vec3 lightSpaceCenter = glm::vec3(lightView * glm::vec4(relativeCenter, 1.0f));
        const float radius = glm::max(bounds.radius, 0.001f);
        return lightSpaceCenter.x + radius >= regionMin.x &&
               lightSpaceCenter.x - radius <= regionMax.x &&
               lightSpaceCenter.y + radius >= regionMin.y &&
               lightSpaceCenter.y - radius <= regionMax.y;
    }

    PlutoGE::render::MeshBounds TransformShadowBounds(const PlutoGE::render::MeshBounds &bounds,
                                                      const glm::mat4 &model)
    {
        const float scaleX = glm::length(glm::vec3(model[0]));
        const float scaleY = glm::length(glm::vec3(model[1]));
        const float scaleZ = glm::length(glm::vec3(model[2]));
        return PlutoGE::render::MeshBounds{
            .center = glm::vec3(model * glm::vec4(bounds.center, 1.0f)),
            .radius = bounds.radius * glm::max(scaleX, glm::max(scaleY, scaleZ)),
        };
    }

    DirectionalShadowDirtyRegion BuildMovedCasterDirtyRegion(
        const std::vector<ShadowCasterEntry> &shadowCasters,
        const DirectionalCascadeProjection &cascadeProjection,
        const glm::vec3 &shadowWorldOrigin,
        int shadowResolution,
        float filterRadiusPixels)
    {
        DirectionalShadowDirtyRegion region;
        glm::vec2 dirtyMin(std::numeric_limits<float>::max());
        glm::vec2 dirtyMax(std::numeric_limits<float>::lowest());

        const auto includeBounds = [&](const PlutoGE::render::MeshBounds &bounds)
        {
            if (!IsBoundsOverlappingDirectionalRegion(
                    bounds,
                    cascadeProjection.lightViewMatrix,
                    shadowWorldOrigin,
                    cascadeProjection.receiverMin,
                    cascadeProjection.receiverMax))
            {
                return;
            }
            const glm::vec3 relativeCenter = bounds.center - shadowWorldOrigin;
            const glm::vec3 lightSpaceCenter = glm::vec3(
                cascadeProjection.lightViewMatrix * glm::vec4(relativeCenter, 1.0f));
            const glm::vec2 radius(glm::max(bounds.radius, 0.001f));
            dirtyMin = glm::min(dirtyMin, glm::vec2(lightSpaceCenter) - radius);
            dirtyMax = glm::max(dirtyMax, glm::vec2(lightSpaceCenter) + radius);
            region.valid = true;
        };

        for (const auto &shadowCaster : shadowCasters)
        {
            if (!shadowCaster.hasMoved)
            {
                continue;
            }
            includeBounds(shadowCaster.bounds);
            includeBounds(shadowCaster.previousBounds);
        }

        if (!region.valid)
        {
            return region;
        }

        const float safeResolution = static_cast<float>(glm::max(shadowResolution, 1));
        const glm::vec2 texelSize = cascadeProjection.receiverExtent / safeResolution;
        const glm::vec2 guard = texelSize * glm::max(filterRadiusPixels + 2.0f, 2.0f);
        dirtyMin = glm::max(dirtyMin - guard, cascadeProjection.receiverMin);
        dirtyMax = glm::min(dirtyMax + guard, cascadeProjection.receiverMax);
        if (glm::any(glm::lessThanEqual(dirtyMax, dirtyMin)))
        {
            region.valid = false;
            return region;
        }

        region.min = dirtyMin;
        region.max = dirtyMax;
        const glm::vec2 normalizedMin = glm::clamp(
            (dirtyMin - cascadeProjection.receiverMin) / cascadeProjection.receiverExtent,
            glm::vec2(0.0f), glm::vec2(1.0f));
        const glm::vec2 normalizedMax = glm::clamp(
            (dirtyMax - cascadeProjection.receiverMin) / cascadeProjection.receiverExtent,
            glm::vec2(0.0f), glm::vec2(1.0f));
        region.pixelX = glm::clamp(static_cast<int>(std::floor(normalizedMin.x * safeResolution)), 0, shadowResolution - 1);
        region.pixelY = glm::clamp(static_cast<int>(std::floor(normalizedMin.y * safeResolution)), 0, shadowResolution - 1);
        const int pixelMaxX = glm::clamp(static_cast<int>(std::ceil(normalizedMax.x * safeResolution)), region.pixelX + 1, shadowResolution);
        const int pixelMaxY = glm::clamp(static_cast<int>(std::ceil(normalizedMax.y * safeResolution)), region.pixelY + 1, shadowResolution);
        region.pixelWidth = pixelMaxX - region.pixelX;
        region.pixelHeight = pixelMaxY - region.pixelY;
        return region;
    }

    std::vector<DirectionalShadowDirtyRegion> BuildMovedCasterDirtyRegions(
        const std::vector<ShadowCasterEntry> &shadowCasters,
        const DirectionalCascadeProjection &cascadeProjection,
        const glm::vec3 &shadowWorldOrigin,
        int shadowResolution,
        float filterRadiusPixels)
    {
        std::vector<DirectionalShadowDirtyRegion> regions;
        regions.reserve(shadowCasters.size());
        const float safeResolution = static_cast<float>(glm::max(shadowResolution, 1));
        const glm::vec2 texelSize = cascadeProjection.receiverExtent / safeResolution;
        const glm::vec2 guard = texelSize * glm::max(filterRadiusPixels + 2.0f, 2.0f);

        const auto buildRegion = [&](const ShadowCasterEntry &shadowCaster)
        {
            DirectionalShadowDirtyRegion region;
            glm::vec2 dirtyMin(std::numeric_limits<float>::max());
            glm::vec2 dirtyMax(std::numeric_limits<float>::lowest());
            const auto includeBounds = [&](const PlutoGE::render::MeshBounds &bounds)
            {
                if (!IsBoundsOverlappingDirectionalRegion(
                        bounds, cascadeProjection.lightViewMatrix, shadowWorldOrigin,
                        cascadeProjection.receiverMin, cascadeProjection.receiverMax))
                {
                    return;
                }
                const glm::vec3 relativeCenter = bounds.center - shadowWorldOrigin;
                const glm::vec3 lightSpaceCenter = glm::vec3(
                    cascadeProjection.lightViewMatrix * glm::vec4(relativeCenter, 1.0f));
                const glm::vec2 radius(glm::max(bounds.radius, 0.001f));
                dirtyMin = glm::min(dirtyMin, glm::vec2(lightSpaceCenter) - radius);
                dirtyMax = glm::max(dirtyMax, glm::vec2(lightSpaceCenter) + radius);
                region.valid = true;
            };
            includeBounds(shadowCaster.previousBounds);
            includeBounds(shadowCaster.bounds);
            if (!region.valid)
                return region;

            dirtyMin = glm::max(dirtyMin - guard, cascadeProjection.receiverMin);
            dirtyMax = glm::min(dirtyMax + guard, cascadeProjection.receiverMax);
            if (glm::any(glm::lessThanEqual(dirtyMax, dirtyMin)))
            {
                region.valid = false;
                return region;
            }

            region.min = dirtyMin;
            region.max = dirtyMax;
            const glm::vec2 normalizedMin = glm::clamp(
                (dirtyMin - cascadeProjection.receiverMin) / cascadeProjection.receiverExtent,
                glm::vec2(0.0f), glm::vec2(1.0f));
            const glm::vec2 normalizedMax = glm::clamp(
                (dirtyMax - cascadeProjection.receiverMin) / cascadeProjection.receiverExtent,
                glm::vec2(0.0f), glm::vec2(1.0f));
            region.pixelX = glm::clamp(static_cast<int>(std::floor(normalizedMin.x * safeResolution)), 0, shadowResolution - 1);
            region.pixelY = glm::clamp(static_cast<int>(std::floor(normalizedMin.y * safeResolution)), 0, shadowResolution - 1);
            const int pixelMaxX = glm::clamp(static_cast<int>(std::ceil(normalizedMax.x * safeResolution)), region.pixelX + 1, shadowResolution);
            const int pixelMaxY = glm::clamp(static_cast<int>(std::ceil(normalizedMax.y * safeResolution)), region.pixelY + 1, shadowResolution);
            region.pixelWidth = pixelMaxX - region.pixelX;
            region.pixelHeight = pixelMaxY - region.pixelY;
            return region;
        };

        const auto mergeRegions = [](DirectionalShadowDirtyRegion &target,
                                     const DirectionalShadowDirtyRegion &source)
        {
            const int minX = std::min(target.pixelX, source.pixelX);
            const int minY = std::min(target.pixelY, source.pixelY);
            const int maxX = std::max(target.pixelX + target.pixelWidth, source.pixelX + source.pixelWidth);
            const int maxY = std::max(target.pixelY + target.pixelHeight, source.pixelY + source.pixelHeight);
            target.pixelX = minX;
            target.pixelY = minY;
            target.pixelWidth = maxX - minX;
            target.pixelHeight = maxY - minY;
            target.min = glm::min(target.min, source.min);
            target.max = glm::max(target.max, source.max);
        };
        const auto overlaps = [](const DirectionalShadowDirtyRegion &a,
                                 const DirectionalShadowDirtyRegion &b)
        {
            return a.pixelX <= b.pixelX + b.pixelWidth &&
                   b.pixelX <= a.pixelX + a.pixelWidth &&
                   a.pixelY <= b.pixelY + b.pixelHeight &&
                   b.pixelY <= a.pixelY + a.pixelHeight;
        };

        for (const auto &shadowCaster : shadowCasters)
        {
            if (!shadowCaster.hasMoved)
                continue;
            auto region = buildRegion(shadowCaster);
            if (!region.valid)
                continue;
            for (std::size_t index = 0; index < regions.size();)
            {
                if (!overlaps(region, regions[index]))
                {
                    ++index;
                    continue;
                }
                mergeRegions(region, regions[index]);
                regions.erase(regions.begin() + static_cast<std::ptrdiff_t>(index));
                index = 0;
            }
            regions.push_back(region);
        }

        // Bound draw repetition when many independent casters are scattered
        // across a cascade. Merge the pair with the smallest additional area
        // until the cluster count is practical.
        constexpr std::size_t kMaximumDirtyRegionClusters = 8;
        while (regions.size() > kMaximumDirtyRegionClusters)
        {
            std::size_t bestA = 0;
            std::size_t bestB = 1;
            std::int64_t bestGrowth = std::numeric_limits<std::int64_t>::max();
            for (std::size_t a = 0; a + 1 < regions.size(); ++a)
            {
                for (std::size_t b = a + 1; b < regions.size(); ++b)
                {
                    const int minX = std::min(regions[a].pixelX, regions[b].pixelX);
                    const int minY = std::min(regions[a].pixelY, regions[b].pixelY);
                    const int maxX = std::max(regions[a].pixelX + regions[a].pixelWidth,
                                              regions[b].pixelX + regions[b].pixelWidth);
                    const int maxY = std::max(regions[a].pixelY + regions[a].pixelHeight,
                                              regions[b].pixelY + regions[b].pixelHeight);
                    const std::int64_t mergedArea = static_cast<std::int64_t>(maxX - minX) * (maxY - minY);
                    const std::int64_t originalArea =
                        static_cast<std::int64_t>(regions[a].pixelWidth) * regions[a].pixelHeight +
                        static_cast<std::int64_t>(regions[b].pixelWidth) * regions[b].pixelHeight;
                    if (mergedArea - originalArea < bestGrowth)
                    {
                        bestGrowth = mergedArea - originalArea;
                        bestA = a;
                        bestB = b;
                    }
                }
            }
            mergeRegions(regions[bestA], regions[bestB]);
            regions.erase(regions.begin() + static_cast<std::ptrdiff_t>(bestB));
        }
        return regions;
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
                           { return shadowCaster.hasMoved && predicate(shadowCaster); });
    }

    void BindShadowMaterialState(PlutoGE::render::Shader *shader, PlutoGE::render::Material *material)
    {
        if (!shader || !material)
        {
            return;
        }

        const auto &config = material->GetConfig();
        auto *albedoTexture = config.albedoTexture;
        const bool alphaTested = albedoTexture && config.alphaMode == PlutoGE::render::AlphaMode::Mask;
        if (alphaTested)
        {
            shader->SetUniform("uAlbedoTexture", albedoTexture, 0);
            shader->SetUniform("uHasAlbedoTexture", 1.0f);
            shader->SetUniform("uAlphaCutoff", config.alphaCutoff);
            return;
        }

        shader->SetUniform("uHasAlbedoTexture", 0.0f);
    }

    void UploadShadowJointMatrices(PlutoGE::render::Shader *shader, const std::vector<glm::mat4> *jointMatrices)
    {
        constexpr size_t kMaxShaderJoints = 128;
        if (!shader || !jointMatrices || jointMatrices->empty())
        {
            if (shader)
                shader->SetUniform("uUseSkinning", 0);
            return;
        }

        shader->SetUniform("uUseSkinning", 1);
        const size_t jointCount = std::min(jointMatrices->size(), kMaxShaderJoints);
        // Array uniforms occupy contiguous locations. Upload the complete pose
        // in one driver call instead of up to 128 calls per bot per cascade.
        shader->SetUniformMatrixArray("uJointMatrices[0]", jointMatrices->data(), jointCount);
    }

    template <typename Predicate, typename LodSelector, typename InstancePredicate>
    ShadowDrawStats DrawShadowCasterBatches(const std::vector<const ShadowCasterEntry *> &sortedShadowCasters,
                                            Predicate &&predicate,
                                            LodSelector &&lodSelector,
                                            InstancePredicate &&instancePredicate,
                                            PlutoGE::render::Shader *shader,
                                            unsigned int &instanceBuffer,
                                            std::size_t &instanceCapacity,
                                            unsigned int &indirectBuffer,
                                            std::size_t &indirectCapacity,
                                            bool &indirectDrawEnabled,
                                            bool &indirectDrawValidated,
                                            std::vector<TransformInstanceData> &batchInstances)
    {
        struct PreparedShadowDraw
        {
            const PlutoGE::render::RenderCommand *command = nullptr;
            std::size_t lodIndex = 0;
            std::size_t firstInstance = 0;
            std::size_t instanceCount = 0;
        };

        ShadowDrawStats stats;
        PlutoGE::render::Material *boundMaterial = nullptr;
        bool hasBoundMaterialState = false;
        bool boundMaterialAlphaTested = false;
        PlutoGE::render::Mesh *boundMesh = nullptr;
        batchInstances.clear();
        batchInstances.reserve(sortedShadowCasters.size());
        // Shadow surfaces are rebuilt repeatedly with similarly sized command
        // sets. Retain CPU scratch capacity across cascades and frames instead
        // of reallocating three command arrays for every updated surface.
        static thread_local std::vector<PreparedShadowDraw> draws;
        draws.clear();
        draws.reserve(sortedShadowCasters.size());

        const auto appendInstances = [&](const PlutoGE::render::RenderCommand &command, std::vector<TransformInstanceData> &instances)
        {
            if (!command.instanceModels || command.instanceModels->empty())
            {
                if (instancePredicate(command, command.model, std::numeric_limits<std::size_t>::max()))
                    instances.push_back(TransformInstanceData{.model = command.model});
                return;
            }

            instances.reserve(instances.size() + command.instanceModels->size());
            for (std::size_t instanceIndex = 0; instanceIndex < command.instanceModels->size(); ++instanceIndex)
            {
                const auto &model = (*command.instanceModels)[instanceIndex];
                if (instancePredicate(command, model, instanceIndex))
                    instances.push_back(TransformInstanceData{.model = model});
            }
        };

        for (const auto *shadowCaster : sortedShadowCasters)
        {
            if (!predicate(*shadowCaster))
            {
                continue;
            }

            const auto *command = shadowCaster->command;
            const std::size_t selectedLodIndex = lodSelector(*shadowCaster);
            const std::size_t previousInstanceCount = batchInstances.size();
            appendInstances(*command, batchInstances);
            const std::size_t appendedInstanceCount = batchInstances.size() - previousInstanceCount;
            if (appendedInstanceCount == 0)
            {
                continue;
            }
            const bool appendToPrevious = !command->jointMatrices &&
                                          !draws.empty() &&
                                          !draws.back().command->jointMatrices &&
                                          CanBatchShadowCommands(*draws.back().command,
                                                                 *command,
                                                                 draws.back().lodIndex,
                                                                 selectedLodIndex);
            if (!appendToPrevious)
            {
                draws.push_back(PreparedShadowDraw{
                    .command = command,
                    .lodIndex = selectedLodIndex,
                    .firstInstance = previousInstanceCount,
                });
            }
            draws.back().instanceCount += appendedInstanceCount;
        }

        // Upload the pass's complete transform stream once. The old path orphaned
        // and repopulated this buffer for every draw, which turns an animated
        // caster invalidating a directional shadow into thousands of driver calls.
        UploadTransformInstances(instanceBuffer, instanceCapacity, batchInstances);

        struct PreparedShadowGroup
        {
            std::size_t firstDraw = 0;
            std::size_t drawCount = 0;
            std::size_t firstIndirectCommand = 0;
            bool usesIndirect = false;
        };

        static thread_local std::vector<PlutoGE::render::DrawElementsIndirectCommand> indirectCommands;
        indirectCommands.clear();
        indirectCommands.reserve(draws.size());
        static thread_local std::vector<PreparedShadowGroup> groups;
        groups.clear();
        groups.reserve(draws.size());
        for (std::size_t drawIndex = 0; drawIndex < draws.size();)
        {
            const auto &head = draws[drawIndex];
            // Skinned submeshes can share one multi-draw as long as they use
            // the same pose. Joint matrices are uniform state, while each
            // indirect command retains its own index range and base instance.
            const bool canUseIndirect = true;
            const bool headAlphaTested = IsAlphaTestedShadowCaster(*head.command);
            const PlutoGE::render::IndirectDrawGroupingKey headKey{
                .material = head.command->material,
                .mesh = head.command->mesh,
                .skinned = head.command->jointMatrices != nullptr,
                .alphaTested = headAlphaTested,
            };
            std::size_t drawEnd = drawIndex + 1;
            if (canUseIndirect)
            {
                while (drawEnd < draws.size())
                {
                    const auto &candidate = draws[drawEnd];
                    const bool candidateAlphaTested = IsAlphaTestedShadowCaster(*candidate.command);
                    const PlutoGE::render::IndirectDrawGroupingKey candidateKey{
                        .material = candidate.command->material,
                        .mesh = candidate.command->mesh,
                        .skinned = candidate.command->jointMatrices != nullptr,
                        .alphaTested = candidateAlphaTested,
                    };
                    const bool compatibleSkinnedDraw = headKey.skinned && candidateKey.skinned &&
                                                       headKey.mesh == candidateKey.mesh &&
                                                       headKey.alphaTested == candidateKey.alphaTested &&
                                                       (!headKey.alphaTested || headKey.material == candidateKey.material);
                    if ((!PlutoGE::render::CanGroupShadowIndirectDraws(headKey, candidateKey) &&
                         !compatibleSkinnedDraw) ||
                        candidate.command->jointMatrices != head.command->jointMatrices)
                    {
                        break;
                    }
                    ++drawEnd;
                }
            }

            // A one-command MDI group still costs one API draw, plus command
            // buffer construction/upload. Submit it directly instead.
            const bool usesIndirect = canUseIndirect && drawEnd - drawIndex > 1;
            const std::size_t firstIndirectCommand = indirectCommands.size();
            if (usesIndirect)
            {
                for (std::size_t groupedDrawIndex = drawIndex; groupedDrawIndex < drawEnd; ++groupedDrawIndex)
                {
                    const auto &groupedDraw = draws[groupedDrawIndex];
                    const auto range = groupedDraw.command->mesh->GetSubmeshLodRange(
                        groupedDraw.command->submeshIndex,
                        groupedDraw.lodIndex);
                    indirectCommands.push_back(PlutoGE::render::BuildDrawElementsIndirectCommand(
                        range.indexCount,
                        groupedDraw.instanceCount,
                        range.indexOffset,
                        groupedDraw.firstInstance));
                }
            }

            groups.push_back(PreparedShadowGroup{
                .firstDraw = drawIndex,
                .drawCount = drawEnd - drawIndex,
                .firstIndirectCommand = firstIndirectCommand,
                .usesIndirect = usesIndirect,
            });
            drawIndex = drawEnd;
        }

        PlutoGE::render::UploadIndirectDrawCommands(indirectBuffer, indirectCapacity, indirectCommands);

        bool skinningEnabled = false;
        const std::vector<glm::mat4> *boundJointMatrices = nullptr;
        for (const auto &group : groups)
        {
            const auto &draw = draws[group.firstDraw];
            const auto &command = *draw.command;
            if (command.material && command.material->GetConfig().twoSided)
            {
                PlutoGE::render::Graphics::Disable(GL_CULL_FACE);
            }
            else
            {
                PlutoGE::render::Graphics::Enable(GL_CULL_FACE);
            }
            const bool alphaTested = IsAlphaTestedShadowCaster(command);
            if (alphaTested)
            {
                if (!hasBoundMaterialState ||
                    !boundMaterialAlphaTested ||
                    command.material != boundMaterial)
                {
                    BindShadowMaterialState(shader, command.material);
                    boundMaterial = command.material;
                    hasBoundMaterialState = true;
                    boundMaterialAlphaTested = true;
                }
            }
            else if (!hasBoundMaterialState || boundMaterialAlphaTested)
            {
                shader->SetUniform("uHasAlbedoTexture", 0.0f);
                boundMaterial = command.material;
                hasBoundMaterialState = true;
                boundMaterialAlphaTested = false;
            }

            if (group.usesIndirect)
            {
                if (command.jointMatrices)
                {
                    if (command.jointMatrices != boundJointMatrices)
                    {
                        UploadShadowJointMatrices(shader, command.jointMatrices);
                        boundJointMatrices = command.jointMatrices;
                    }
                    skinningEnabled = true;
                }
                else if (skinningEnabled)
                {
                    shader->SetUniform("uUseSkinning", 0);
                    skinningEnabled = false;
                    boundJointMatrices = nullptr;
                }
                if (command.mesh != boundMesh)
                {
                    BindTransformInstanceAttributes(*command.mesh, instanceBuffer, 0);
                    boundMesh = command.mesh;
                }
                bool submittedIndirectly = false;
                if (indirectDrawEnabled)
                {
                    const bool validateIndirectDraw = !indirectDrawValidated;
                    if (validateIndirectDraw)
                    {
                        while (glGetError() != GL_NO_ERROR)
                        {
                        }
                    }
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
                    glMultiDrawElementsIndirect(
                        GL_TRIANGLES,
                        GL_UNSIGNED_INT,
                        reinterpret_cast<const void *>(group.firstIndirectCommand * sizeof(PlutoGE::render::DrawElementsIndirectCommand)),
                        static_cast<GLsizei>(group.drawCount),
                        0);
                    if (validateIndirectDraw)
                    {
                        const GLenum indirectError = glGetError();
                        submittedIndirectly = indirectError == GL_NO_ERROR;
                        if (!submittedIndirectly)
                        {
                            std::cerr << "Shadow multi-draw disabled after OpenGL error " << indirectError
                                      << "; using direct draws." << std::endl;
                            indirectDrawEnabled = false;
                        }
                        else
                        {
                            indirectDrawValidated = true;
                        }
                    }
                    else
                    {
                        submittedIndirectly = true;
                    }
                }

                if (!submittedIndirectly)
                {
                    for (std::size_t groupedDrawIndex = group.firstDraw;
                         groupedDrawIndex < group.firstDraw + group.drawCount;
                         ++groupedDrawIndex)
                    {
                        const auto &directDraw = draws[groupedDrawIndex];
                        directDraw.command->mesh->DrawSubmeshInstancedBaseInstanceBound(
                            directDraw.command->submeshIndex,
                            directDraw.instanceCount,
                            directDraw.firstInstance,
                            directDraw.lodIndex);
                    }
                    stats.apiDrawCalls += static_cast<int>(group.drawCount);
                }
                else
                {
                    ++stats.apiDrawCalls;
                }
            }
            else
            {
                if (command.jointMatrices)
                {
                    if (command.jointMatrices != boundJointMatrices)
                    {
                        UploadShadowJointMatrices(shader, command.jointMatrices);
                        boundJointMatrices = command.jointMatrices;
                    }
                    skinningEnabled = true;
                }
                else if (skinningEnabled)
                {
                    shader->SetUniform("uUseSkinning", 0);
                    skinningEnabled = false;
                    boundJointMatrices = nullptr;
                }
                BindTransformInstanceAttributes(*command.mesh, instanceBuffer, 0);
                command.mesh->DrawSubmeshInstancedBaseInstanceBound(command.submeshIndex,
                                                                    draw.instanceCount,
                                                                    draw.firstInstance,
                                                                    draw.lodIndex);
                boundMesh = command.mesh;
                ++stats.apiDrawCalls;
            }

            for (std::size_t groupedDrawIndex = group.firstDraw;
                 groupedDrawIndex < group.firstDraw + group.drawCount;
                 ++groupedDrawIndex)
            {
                const auto &groupedDraw = draws[groupedDrawIndex];
                const auto &groupedCommand = *groupedDraw.command;
                const auto indexCount = groupedCommand.mesh->GetSubmeshLodIndexCount(groupedCommand.submeshIndex, groupedDraw.lodIndex);
                stats.submittedInstances += static_cast<int>(groupedDraw.instanceCount);
                stats.submittedTriangles += static_cast<int>((indexCount / 3) * groupedDraw.instanceCount);
                ++stats.submittedBatches;
            }
        }

        stats.materialGroups = static_cast<int>(groups.size());
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

        if (skinningEnabled)
        {
            shader->SetUniform("uUseSkinning", 0);
        }

        PlutoGE::render::Graphics::Enable(GL_CULL_FACE);
        return stats;
    }
}

namespace PlutoGE::render
{
    class ShadowPassCache
    {
    public:
        struct Key
        {
            const std::vector<glm::mat4> *instances = nullptr;
            const Mesh *mesh = nullptr;
            std::uint32_t submeshIndex = 0;

            bool operator==(const Key &) const = default;
        };

        struct KeyHash
        {
            std::size_t operator()(const Key &key) const noexcept
            {
                std::size_t hash = std::hash<const void *>{}(key.instances);
                hash ^= std::hash<const void *>{}(key.mesh) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
                hash ^= std::hash<std::uint32_t>{}(key.submeshIndex) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
                return hash;
            }
        };

        struct Entry
        {
            std::weak_ptr<const std::vector<glm::mat4>> owner;
            std::vector<MeshBounds> bounds;
        };

        std::unordered_map<Key, Entry, KeyHash> staticInstanceBounds;
    };

    ShadowPass::ShadowPass()
        : m_cache(std::make_unique<ShadowPassCache>())
    {
    }

    ShadowPass::~ShadowPass()
    {
        glDeleteBuffers(static_cast<GLsizei>(m_instanceBuffers.size()), m_instanceBuffers.data());
        glDeleteBuffers(static_cast<GLsizei>(m_indirectBuffers.size()), m_indirectBuffers.data());
        if (m_shadowFramebuffer != 0)
            Graphics::DeleteFramebuffers(1, &m_shadowFramebuffer);
    }

    void ShadowPass::Initialize()
    {
        m_shadowPassShader = Shader::CreateShadowPassShader();
        glGenFramebuffers(1, &m_shadowFramebuffer);
        glGenBuffers(static_cast<GLsizei>(m_instanceBuffers.size()), m_instanceBuffers.data());
    }

    bool ShadowPass::CanSkipStaticFrame(const RenderContext &ctx) const
    {
        if (!m_hasShadowCasterFingerprint || !m_allCachedShadowCastersStatic ||
            !ctx.lights || !ctx.hasCameraData)
        {
            return false;
        }

        const bool cameraDataChanged = !ctx.hasPreviousCameraData ||
                                       HasDirectionalCameraOrientationOrProjectionChanged(ctx.cameraData, ctx.previousCameraData);
        bool hasShadowLight = false;
        for (const auto *light : *ctx.lights)
        {
            if (!light || !light->castsShadows)
            {
                continue;
            }
            hasShadowLight = true;
            if (light->isDirty || light->shadowRefreshPending ||
                light->pendingShadowCascadeMask != 0 || light->pendingPointShadowFaceMask != 0)
            {
                return false;
            }
            if (light->type != scene::LightType::Directional)
            {
                continue;
            }
            if (cameraDataChanged)
            {
                return false;
            }

            const glm::vec3 currentOrigin = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
            const int cascadeCount = GetDirectionalCascadeCount(*light);
            const auto splits = BuildDirectionalCascadeSplits(ctx.cameraData, light->directionalShadowSettings, cascadeCount);
            for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
            {
                const bool hasStoredOrigin =
                    !AreMatricesApproximatelyEqual(light->shadowCascadeMatrices[cascadeIndex], glm::mat4(1.0f)) ||
                    glm::length(light->shadowCascadeWorldOrigins[cascadeIndex]) > kShadowUpdateMatrixEpsilon;
                const float cascadeNear = cascadeIndex == 0 ? ctx.cameraData.nearPlane : splits[cascadeIndex - 1];
                if (!hasStoredOrigin ||
                    ShouldRefreshCameraRelativeCascade(currentOrigin,
                                                       light->shadowCascadeWorldOrigins[cascadeIndex],
                                                       cascadeNear,
                                                       splits[cascadeIndex],
                                                       GetShadowResolution(*light)))
                {
                    return false;
                }
            }
        }
        return hasShadowLight;
    }

    void ShadowPass::Execute(const RenderContext &ctx)
    {
        if (!m_shadowPassShader || !ctx.lights || !ctx.renderCommands || m_shadowFramebuffer == 0)
        {
            return;
        }

        bool hasShadowWork = false;
        for (auto *light : *ctx.lights)
        {
            if (light && light->castsShadows)
            {
                hasShadowWork = true;
                break;
            }
        }
        if (!hasShadowWork)
        {
            return;
        }

        if (CanSkipStaticFrame(ctx))
        {
            return;
        }

        const ShadowCasterFrameState casterFrameState = InspectShadowCasters(*ctx.renderCommands);
        const glm::vec3 shadowCameraPosition = ctx.hasCameraData
                                                   ? glm::vec3(glm::inverse(ctx.cameraData.view)[3])
                                                   : glm::vec3(0.0f);
        const auto resolveInstanceBounds = [&](const PlutoGE::render::RenderCommand &command,
                                               const glm::mat4 &model,
                                               std::size_t instanceIndex)
        {
            if (!command.isStatic || !command.instanceModels ||
                instanceIndex >= command.instanceModels->size() || !command.mesh ||
                command.submeshIndex >= command.mesh->GetSubmeshCount())
            {
                return command.mesh && command.submeshIndex < command.mesh->GetSubmeshCount()
                           ? TransformShadowBounds(command.mesh->GetSubmesh(command.submeshIndex).bounds, model)
                           : MeshBounds{};
            }

            const ShadowPassCache::Key key{
                .instances = command.instanceModels.get(),
                .mesh = command.mesh,
                .submeshIndex = command.submeshIndex,
            };
            auto &entry = m_cache->staticInstanceBounds[key];
            const auto owner = entry.owner.lock();
            if (owner.get() != command.instanceModels.get() ||
                entry.bounds.size() != command.instanceModels->size())
            {
                entry.owner = command.instanceModels;
                entry.bounds.clear();
                entry.bounds.reserve(command.instanceModels->size());
                const auto &sourceBounds = command.mesh->GetSubmesh(command.submeshIndex).bounds;
                for (const auto &instanceModel : *command.instanceModels)
                    entry.bounds.push_back(TransformShadowBounds(sourceBounds, instanceModel));
            }
            return entry.bounds[instanceIndex];
        };
        const auto passesInstanceShadowDistance = [&](const PlutoGE::render::RenderCommand &command,
                                                      const glm::mat4 &model,
                                                      std::size_t instanceIndex)
        {
            if (!ctx.hasCameraData || command.maxShadowDistance <= 0.0f ||
                command.maxShadowDistance == std::numeric_limits<float>::max())
            {
                return true;
            }
            if (!command.mesh || command.submeshIndex >= command.mesh->GetSubmeshCount())
            {
                return false;
            }

            const auto bounds = resolveInstanceBounds(command, model, instanceIndex);
            const float maximumCenterDistance = command.maxShadowDistance +
                                                glm::max(bounds.radius, 0.0f);
            const glm::vec3 offset = bounds.center - shadowCameraPosition;
            return glm::dot(offset, offset) <= maximumCenterDistance * maximumCenterDistance;
        };
        m_allCachedShadowCastersStatic = casterFrameState.allCastersStatic;
        const bool shadowCasterTopologyChanged = !m_hasShadowCasterFingerprint ||
                                                 casterFrameState.fingerprint != m_shadowCasterFingerprint;
        if (shadowCasterTopologyChanged)
        {
            std::erase_if(m_cache->staticInstanceBounds, [](const auto &item)
                          { return item.second.owner.expired(); });
        }
        bool shadowCastersChanged = casterFrameState.hasMovedCaster || shadowCasterTopologyChanged;
        const bool cameraDataChanged = ctx.hasCameraData &&
                                       (!ctx.hasPreviousCameraData ||
                                        HasDirectionalCameraOrientationOrProjectionChanged(ctx.cameraData, ctx.previousCameraData));
        bool needsAnyShadowUpdate = shadowCastersChanged;
        for (auto *light : *ctx.lights)
        {
            if (!light || !light->castsShadows)
            {
                continue;
            }

            const bool hasPendingRefresh =
                light->isDirty ||
                light->shadowRefreshPending ||
                light->pendingShadowCascadeMask != 0 ||
                light->pendingPointShadowFaceMask != 0;
            if (hasPendingRefresh)
            {
                needsAnyShadowUpdate = true;
                break;
            }

            if (light->type != scene::LightType::Directional || !ctx.hasCameraData)
            {
                continue;
            }

            if (cameraDataChanged)
            {
                needsAnyShadowUpdate = true;
                break;
            }

            const glm::vec3 currentShadowWorldOrigin = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
            const int cascadeCount = GetDirectionalCascadeCount(*light);
            const auto cascadeSplits = BuildDirectionalCascadeSplits(ctx.cameraData, light->directionalShadowSettings, cascadeCount);
            for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
            {
                const bool hasStoredCascadeOrigin =
                    !AreMatricesApproximatelyEqual(light->shadowCascadeMatrices[cascadeIndex], glm::mat4(1.0f)) ||
                    glm::length(light->shadowCascadeWorldOrigins[cascadeIndex]) > kShadowUpdateMatrixEpsilon;
                const float cascadeNear = cascadeIndex == 0 ? ctx.cameraData.nearPlane : cascadeSplits[cascadeIndex - 1];
                const float cascadeFar = cascadeSplits[cascadeIndex];
                if (!hasStoredCascadeOrigin ||
                    ShouldRefreshCameraRelativeCascade(currentShadowWorldOrigin,
                                                       light->shadowCascadeWorldOrigins[cascadeIndex],
                                                       cascadeNear,
                                                       cascadeFar,
                                                       GetShadowResolution(*light)))
                {
                    needsAnyShadowUpdate = true;
                    break;
                }
            }
            if (needsAnyShadowUpdate)
            {
                break;
            }
        }

        if (!needsAnyShadowUpdate)
        {
            return;
        }

        m_shadowCasterFingerprint = casterFrameState.fingerprint;
        m_hasShadowCasterFingerprint = true;

        GLint previousViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        Graphics::BindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        Graphics::Enable(GL_DEPTH_TEST);
        Graphics::Disable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDepthRange(0.0, 1.0);
        glClearDepth(1.0);
        Graphics::Enable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        Graphics::Disable(GL_POLYGON_OFFSET_FILL);

        m_shadowPassShader->Bind();
        m_shadowPassShader->SetUniform("uShadowWorldOrigin", glm::vec3(0.0f));
        static thread_local std::vector<ShadowCasterEntry> shadowCasters;
        shadowCasters.clear();
        shadowCasters.reserve(ctx.renderCommands->size());
        if (shadowCasterTopologyChanged ||
            m_sortedShadowCasterCommandIndices.size() != casterFrameState.casterCount)
        {
            m_sortedShadowCasterCommandIndices = BuildSortedShadowCasterCommandIndices(*ctx.renderCommands);
        }
        bool movedShadowCaster = false;
        BuildShadowCasterEntries(
            *ctx.renderCommands,
            m_sortedShadowCasterCommandIndices,
            shadowCasters,
            movedShadowCaster);
        shadowCastersChanged = shadowCastersChanged || movedShadowCaster;
        std::vector<const ShadowCasterEntry *> sortedShadowCasters;
        sortedShadowCasters.reserve(shadowCasters.size());
        for (const auto &shadowCaster : shadowCasters)
            sortedShadowCasters.push_back(&shadowCaster);
        static thread_local std::vector<TransformInstanceData> shadowBatchInstances;
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

            // Preserve caster-driven work explicitly so it can be consumed over
            // several frames even after the caster's moved flag has cleared.
            if (!light->isDirty && shadowCastersChanged)
            {
                if (light->type == scene::LightType::Point)
                {
                    light->pendingPointShadowFaceMask |= kAllPointShadowFacesMask;
                }
                else if (light->type == scene::LightType::Spot)
                {
                    light->shadowRefreshPending = true;
                }
            }

            const bool hasPendingDirectionalRefresh = light->type == scene::LightType::Directional && light->pendingShadowCascadeMask != 0;
            const bool hasPendingPointRefresh = light->type == scene::LightType::Point && light->pendingPointShadowFaceMask != 0;
            const bool hasPendingIncrementalRefresh = (light->shadowRefreshPending || hasPendingDirectionalRefresh || hasPendingPointRefresh) && !light->isDirty;
            bool deferredShadowRefresh = false;
            bool directionalCascadeOriginMismatch = false;
            std::uint8_t directionalCascadeOriginMismatchMask = 0;
            if (light->type == scene::LightType::Directional && ctx.hasCameraData)
            {
                const glm::vec3 currentShadowWorldOrigin = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
                const int cascadeCount = GetDirectionalCascadeCount(*light);
                const auto cascadeSplits = BuildDirectionalCascadeSplits(ctx.cameraData, light->directionalShadowSettings, cascadeCount);
                for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                {
                    const bool hasStoredCascadeOrigin =
                        !AreMatricesApproximatelyEqual(light->shadowCascadeMatrices[cascadeIndex], glm::mat4(1.0f)) ||
                        glm::length(light->shadowCascadeWorldOrigins[cascadeIndex]) > kShadowUpdateMatrixEpsilon;
                    const float cascadeNear = cascadeIndex == 0 ? ctx.cameraData.nearPlane : cascadeSplits[cascadeIndex - 1];
                    const float cascadeFar = cascadeSplits[cascadeIndex];
                    if (!hasStoredCascadeOrigin ||
                        ShouldRefreshCameraRelativeCascade(currentShadowWorldOrigin,
                                                           light->shadowCascadeWorldOrigins[cascadeIndex],
                                                           cascadeNear,
                                                           cascadeFar,
                                                           GetShadowResolution(*light)))
                    {
                        directionalCascadeOriginMismatch = true;
                        directionalCascadeOriginMismatchMask |= static_cast<std::uint8_t>(1u << cascadeIndex);
                    }
                }
            }
            const bool motionDrivenDirectionalInvalidation =
                light->type == scene::LightType::Directional &&
                ctx.hasCameraData &&
                (cameraDataChanged || shadowCastersChanged || hasPendingIncrementalRefresh || directionalCascadeOriginMismatch);

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

                Graphics::Disable(GL_POLYGON_OFFSET_FILL);
                const float farPlane = glm::max(light->range, 0.1f);
                light->shadowFarPlane = farPlane;
                const auto shadowMatrices = BuildPointShadowMatrices(*light, farPlane);
                if (!light->isDirty && light->shadowRefreshPending && light->pendingPointShadowFaceMask == 0)
                {
                    light->pendingPointShadowFaceMask = kAllPointShadowFacesMask;
                }
                Graphics::SetViewport(0, 0, shadowMap->GetWidth(), shadowMap->GetHeight());

                m_shadowPassShader->SetUniform("uShadowPassMode", kPointShadowPassMode);
                m_shadowPassShader->SetUniform("uLightPosition", light->position);
                m_shadowPassShader->SetUniform("uFarPlane", farPlane);

                for (unsigned int face = 0; face < shadowMatrices.size(); ++face)
                {
                    const auto faceFrustumPlanes = ExtractFrustumPlanes(shadowMatrices[face]);
                    const std::uint8_t faceBit = static_cast<std::uint8_t>(1u << face);
                    const bool urgentMovedCasterFace = AnyMovedShadowCasterRelevant(
                        shadowCasters,
                        [&](const ShadowCasterEntry &shadowCaster)
                        {
                            return IsMovedCommandRelevantForPointLight(shadowCaster, *light) &&
                                   IsMovedCommandRelevantForProjectedLight(shadowCaster, faceFrustumPlanes);
                        });
                    bool faceNeedsIncrementalRefresh = (light->pendingPointShadowFaceMask & faceBit) != 0;
                    if (!light->isDirty && !faceNeedsIncrementalRefresh)
                    {
                        faceNeedsIncrementalRefresh = urgentMovedCasterFace;
                    }

                    if (!light->isDirty)
                    {
                        if (!faceNeedsIncrementalRefresh)
                        {
                            continue;
                        }

                        if (!urgentMovedCasterFace && !reserveIncrementalShadowSurfaceUpdate())
                        {
                            deferredShadowRefresh = true;
                            continue;
                        }
                    }

                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, shadowMap->GetTextureID(), 0);
                    if (!ValidateShadowFramebuffer("point shadow", shadowMap->GetTextureID()))
                    {
                        light->pendingPointShadowFaceMask |= faceBit;
                        deferredShadowRefresh = true;
                        continue;
                    }
                    glClear(GL_DEPTH_BUFFER_BIT);
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrices[face]);
                    const std::size_t streamBufferIndex = AcquireStreamBuffer();
                    const ShadowDrawStats drawStats = DrawShadowCasterBatches(
                        sortedShadowCasters,
                        [&](const ShadowCasterEntry &shadowCaster)
                        {
                            return IsCommandRelevantForPointLight(shadowCaster, *light) &&
                                   IsCommandRelevantForProjectedLight(shadowCaster, faceFrustumPlanes);
                        },
                        [](const ShadowCasterEntry &shadowCaster)
                        {
                            return SelectDefaultShadowLod(*shadowCaster.command);
                        },
                        [&](const PlutoGE::render::RenderCommand &command, const glm::mat4 &model, std::size_t instanceIndex)
                        {
                            return passesInstanceShadowDistance(command, model, instanceIndex);
                        },
                        m_shadowPassShader,
                        m_instanceBuffers[streamBufferIndex],
                        m_instanceCapacities[streamBufferIndex],
                        m_indirectBuffers[streamBufferIndex],
                        m_indirectCapacities[streamBufferIndex],
                        m_indirectDrawEnabled,
                        m_indirectDrawValidated,
                        shadowBatchInstances);
                    if (ctx.renderer)
                    {
                        ctx.renderer->RecordShadowMapUpdate(
                            shadowMap->GetWidth() * shadowMap->GetHeight(),
                            drawStats.submittedInstances,
                            drawStats.submittedBatches,
                            drawStats.submittedTriangles,
                            drawStats.materialGroups,
                            drawStats.apiDrawCalls,
                            false);
                    }
                    light->pendingPointShadowFaceMask &= static_cast<std::uint8_t>(~faceBit);
                }

                light->isDirty = false;
                light->shadowRefreshPending = deferredShadowRefresh || light->pendingPointShadowFaceMask != 0;
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
                std::uint8_t movedCasterCascadeMask = BuildMovedCasterCascadeMask(
                    shadowCasters,
                    ctx.cameraData,
                    cascadeSplits,
                    cascadeCount,
                    light->directionalShadowSettings.cascadeBlendDistance);
                const bool hasAnimatedCasterMotion = std::any_of(
                    shadowCasters.begin(), shadowCasters.end(),
                    [](const ShadowCasterEntry &caster)
                    {
                        return caster.command && caster.command->skinningPoseChanged;
                    });
                // A camera-relative origin recenter is normal traversal work. Keep
                // using the old cascade until its queued replacement is rendered;
                // only initialization or an explicit light/settings change redraws
                // every cascade synchronously.
                const bool forceFullCascadeUpdate = light->isDirty || !ctx.hasPreviousCameraData;
                const bool casterOnlyCascadeInvalidation = shadowCastersChanged && !light->isDirty && !cameraDataChanged && !hasPendingIncrementalRefresh;
                const bool cameraOnlyInvalidation = cameraDataChanged && !light->isDirty && !shadowCastersChanged;
                const std::uint8_t allCascadeMask = static_cast<std::uint8_t>((1u << cascadeCount) - 1u);
                // The two nearest cascades carry the visible detail from animated
                // characters. Joint motion does not change a command's model
                // matrix, and bind-pose bounds cannot describe the exact swept
                // silhouette, so refresh both immediately whenever a skin pose
                // changes instead of relying solely on depth-range classification.
                const std::uint8_t animatedMotionCascadeMask = hasAnimatedCasterMotion
                                                                   ? static_cast<std::uint8_t>(allCascadeMask & 0x03u)
                                                                   : 0u;
                movedCasterCascadeMask |= animatedMotionCascadeMask;
                if (forceFullCascadeUpdate || cameraDataChanged)
                {
                    light->pendingShadowCascadeMask |= allCascadeMask;
                }
                else if (shadowCastersChanged)
                {
                    // Transform motion only dirties the depth ranges touched
                    // by that caster. Topology/material changes do not provide
                    // moved bounds, so conservatively invalidate every cascade.
                    light->pendingShadowCascadeMask |= movedCasterCascadeMask != 0
                                                           ? movedCasterCascadeMask
                                                           : allCascadeMask;
                }
                else if (directionalCascadeOriginMismatch)
                {
                    light->pendingShadowCascadeMask |= directionalCascadeOriginMismatchMask;
                }
                else if (light->shadowRefreshPending && light->pendingShadowCascadeMask == 0)
                {
                    // Convert refreshes queued by older/general shadow paths into
                    // explicit cascade work rather than repeatedly starting at zero.
                    light->pendingShadowCascadeMask = allCascadeMask;
                }

                std::uint8_t scheduledCascadeMask = 0;
                if (!forceFullCascadeUpdate)
                {
                    scheduledCascadeMask |= animatedMotionCascadeMask;
                    // The nearest cascade covers first-person geometry and the
                    // player's local ground shadow, so never defer it behind a
                    // farther weighted update. A second incremental-update slot
                    // lets the weighted schedule continue draining far cascades.
                    const std::uint8_t nearCascadeBit = 1u;
                    if ((light->pendingShadowCascadeMask & nearCascadeBit) != 0)
                    {
                        scheduledCascadeMask |= nearCascadeBit;
                    }

                    for (std::size_t attempt = 0; attempt < kDirectionalCascadeRefreshSchedule.size(); ++attempt)
                    {
                        const int scheduleIndex = light->nextShadowCascadeToRefresh % static_cast<int>(kDirectionalCascadeRefreshSchedule.size());
                        light->nextShadowCascadeToRefresh = (scheduleIndex + 1) % static_cast<int>(kDirectionalCascadeRefreshSchedule.size());
                        const int candidateCascadeIndex = kDirectionalCascadeRefreshSchedule[static_cast<std::size_t>(scheduleIndex)];
                        const std::uint8_t candidateCascadeBit = static_cast<std::uint8_t>(1u << candidateCascadeIndex);
                        if (candidateCascadeIndex < cascadeCount &&
                            (light->pendingShadowCascadeMask & candidateCascadeBit) != 0)
                        {
                            scheduledCascadeMask |= candidateCascadeBit;
                            break;
                        }
                    }
                }

                light->shadowMatrix = glm::mat4(1.0f);
                light->shadowFarPlane = cascadeSplits[cascadeCount - 1];
                // Directional cascades need raster-space slope bias. Receiver
                // normal bias alone loses effectiveness as a surface becomes
                // parallel to the light and produces acne on steep terrain.
                Graphics::Enable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(kDirectionalShadowSlopeBias, kDirectionalShadowConstantBias);
                Graphics::Disable(GL_CULL_FACE);
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
                    const float effectiveMinCasterTexelRadius = glm::max(
                        light->directionalShadowSettings.minCasterTexelRadius,
                        cascadeIndex == 0 ? kNearCascadeMinCasterTexelRadius : kFarCascadeMinCasterTexelRadius);
                    const bool hasStoredCascadeOrigin = !AreMatricesApproximatelyEqual(light->shadowCascadeMatrices[cascadeIndex], glm::mat4(1.0f)) ||
                                                        glm::length(light->shadowCascadeWorldOrigins[cascadeIndex]) > kShadowUpdateMatrixEpsilon;
                    const bool cascadeOriginChanged = ShouldRefreshCameraRelativeCascade(
                        currentShadowWorldOrigin,
                        light->shadowCascadeWorldOrigins[cascadeIndex],
                        cascadeNear,
                        cascadeFar,
                        shadowResolution);
                    const glm::vec3 cascadeShadowWorldOrigin = (forceFullCascadeUpdate || !hasStoredCascadeOrigin || cascadeOriginChanged)
                                                                   ? currentShadowWorldOrigin
                                                                   : light->shadowCascadeWorldOrigins[cascadeIndex];
                    // Split radii drive cascade selection in lighting, so keep them current even when
                    // this cascade's shadow map redraw is deferred by the update cadence.
                    light->shadowCascadeSplits[cascadeIndex] = cascadeFar;

                    if (!forceFullCascadeUpdate &&
                        (scheduledCascadeMask & static_cast<std::uint8_t>(1u << cascadeIndex)) == 0)
                    {
                        continue;
                    }

                    const auto cascadeProjection = BuildDirectionalCascadeProjection(*light, ctx.cameraData, cascadeShadowWorldOrigin, cascadeNear, cascadeFar, shadowResolution);
                    const glm::mat4 &cascadeMatrix = cascadeProjection.lightSpaceMatrix;
                    const bool cascadeMatrixChanged = !AreMatricesApproximatelyEqual(cascadeMatrix, light->shadowCascadeMatrices[cascadeIndex]);
                    const DirectionalShadowScroll cascadeScroll =
                        // A camera-relative origin recenter is the primary reason
                        // to scroll a cached cascade. ResolveDirectionalShadowScroll
                        // validates that the resulting matrix change is only an
                        // integer-texel XY translation before reuse is allowed.
                        !forceFullCascadeUpdate && !cascadeSplitChanged && !shadowCasterTopologyChanged
                            ? ResolveDirectionalShadowScroll(
                                  light->shadowCascadeMatrices[cascadeIndex],
                                  light->shadowCascadeWorldOrigins[cascadeIndex],
                                  cascadeMatrix,
                                  cascadeShadowWorldOrigin,
                                  shadowResolution)
                            : DirectionalShadowScroll{};
                    const glm::mat4 &cascadeRenderMatrix = cascadeScroll.valid
                                                               ? cascadeScroll.resolvedRelativeMatrix
                                                               : cascadeMatrix;
                    if (ctx.renderer)
                    {
                        const bool scrollCandidate = !forceFullCascadeUpdate && !cascadeSplitChanged && !shadowCasterTopologyChanged;
                        ctx.renderer->RecordDirectionalShadowScroll(
                            scrollCandidate,
                            cascadeScroll.valid,
                            !forceFullCascadeUpdate && !cascadeSplitChanged && shadowCasterTopologyChanged,
                            cascadeScroll.maxMatrixDelta,
                            cascadeScroll.fractionalTexelError);
                    }
                    if (cameraOnlyInvalidation && !cascadeMatrixChanged && !cascadeSplitChanged && !cascadeOriginChanged)
                    {
                        light->pendingShadowCascadeMask &= static_cast<std::uint8_t>(~(1u << cascadeIndex));
                        continue;
                    }

                    const std::uint8_t cascadeBit = static_cast<std::uint8_t>(1u << cascadeIndex);
                    if (casterOnlyCascadeInvalidation &&
                        (animatedMotionCascadeMask & cascadeBit) == 0 &&
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
                                    effectiveMinCasterTexelRadius);
                            }))
                    {
                        light->pendingShadowCascadeMask &= static_cast<std::uint8_t>(~(1u << cascadeIndex));
                        continue;
                    }

                    const bool urgentMovedCasterUpdate =
                        (movedCasterCascadeMask & cascadeBit) != 0;
                    if (!forceFullCascadeUpdate &&
                        !urgentMovedCasterUpdate &&
                        !reserveIncrementalShadowSurfaceUpdate())
                    {
                        deferredShadowRefresh = true;
                        continue;
                    }

                    light->shadowCascadeWorldOrigins[cascadeIndex] = cascadeShadowWorldOrigin;
                    light->shadowCascadeMatrices[cascadeIndex] = cascadeRenderMatrix;

                    Graphics::SetViewport(0, 0, shadowResolution, shadowResolution);
                    m_shadowPassShader->SetUniform("uShadowWorldOrigin", light->shadowCascadeWorldOrigins[cascadeIndex]);
                    auto *staticCascadeMap = light->staticShadowCascadeMaps[cascadeIndex].get();
                    if (!staticCascadeMap)
                    {
                        continue;
                    }
                    const bool movedStaticCaster = std::any_of(
                        shadowCasters.begin(), shadowCasters.end(),
                        [](const ShadowCasterEntry &caster)
                        {
                            return caster.hasMoved && caster.command->isStatic && !caster.command->jointMatrices;
                        });
                    const bool staticNeedsFullRefresh = !light->staticShadowCascadeValid[cascadeIndex] ||
                                                        forceFullCascadeUpdate || shadowCasterTopologyChanged ||
                                                        movedStaticCaster || !cascadeScroll.valid;
                    const bool requiresScrollCopy = !staticNeedsFullRefresh &&
                                                    (cascadeScroll.x != 0 || cascadeScroll.y != 0);
                    unsigned int renderDepthTexture = staticCascadeMap->GetTextureID();
                    std::uint8_t activeScratchIndex = 0;
                    if (requiresScrollCopy)
                    {
                        activeScratchIndex = static_cast<std::uint8_t>(
                            light->nextStaticShadowScratchIndex[cascadeIndex] % 2u);
                        auto &scratchMap = light->staticShadowCascadeScratchMaps[cascadeIndex][activeScratchIndex];
                        if (!scratchMap || scratchMap->GetWidth() != shadowResolution ||
                            scratchMap->GetHeight() != shadowResolution)
                        {
                            scratchMap.reset(Texture::DepthTexture(shadowResolution, shadowResolution));
                        }
                        const unsigned int scratchTexture = scratchMap->GetTextureID();
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, scratchTexture, 0);
                        Graphics::Disable(GL_SCISSOR_TEST);

                        const int sourceX = glm::max(0, -cascadeScroll.x);
                        const int sourceY = glm::max(0, -cascadeScroll.y);
                        const int destinationX = glm::max(0, cascadeScroll.x);
                        const int destinationY = glm::max(0, cascadeScroll.y);
                        const int copyWidth = shadowResolution - std::abs(cascadeScroll.x);
                        const int copyHeight = shadowResolution - std::abs(cascadeScroll.y);
                        glClear(GL_DEPTH_BUFFER_BIT);
                        glCopyImageSubData(staticCascadeMap->GetTextureID(), GL_TEXTURE_2D, 0, sourceX, sourceY, 0,
                                           scratchTexture, GL_TEXTURE_2D, 0, destinationX, destinationY, 0,
                                           copyWidth, copyHeight, 1);
                        renderDepthTexture = scratchTexture;
                    }

                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, renderDepthTexture, 0);
                    if (!ValidateShadowFramebuffer(
                            "directional cascade shadow", renderDepthTexture))
                    {
                        continue;
                    }
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", cascadeRenderMatrix);
                    ShadowDrawStats drawStats;
                    int updatedPixels = 0;
                    const auto lightSpaceRegionForPixels = [&](int x, int y, int width, int height)
                    {
                        const glm::vec2 scale = cascadeProjection.receiverExtent /
                                                static_cast<float>(shadowResolution);
                        DirectionalShadowDirtyRegion region;
                        region.min = cascadeProjection.receiverMin + glm::vec2(x, y) * scale;
                        region.max = region.min + glm::vec2(width, height) * scale;
                        region.pixelX = x;
                        region.pixelY = y;
                        region.pixelWidth = width;
                        region.pixelHeight = height;
                        region.valid = true;
                        return region;
                    };
                    const auto drawRegion = [&](int x, int y, int width, int height,
                                                const DirectionalShadowDirtyRegion *lightSpaceRegion,
                                                bool clearDepth,
                                                bool drawStaticCasters)
                    {
                        if (width <= 0 || height <= 0)
                            return;
                        int drawX = x;
                        int drawY = y;
                        int drawWidth = width;
                        int drawHeight = height;
                        const DirectionalShadowDirtyRegion *effectiveRegion = lightSpaceRegion;

                        const bool scissored = drawWidth != shadowResolution || drawHeight != shadowResolution || drawX != 0 || drawY != 0;
                        if (scissored)
                        {
                            Graphics::Enable(GL_SCISSOR_TEST);
                            glScissor(drawX, drawY, drawWidth, drawHeight);
                        }
                        else
                            Graphics::Disable(GL_SCISSOR_TEST);
                        if (clearDepth)
                            glClear(GL_DEPTH_BUFFER_BIT);
                        const std::size_t streamBufferIndex = AcquireStreamBuffer();
                        const ShadowDrawStats regionStats = DrawShadowCasterBatches(
                            sortedShadowCasters,
                            [&](const ShadowCasterEntry &shadowCaster)
                            {
                                const bool relevant = IsCommandRelevantForDirectionalCascade(
                                    shadowCaster, cascadeProjection.lightViewMatrix, cascadeShadowWorldOrigin,
                                    cascadeProjection.receiverMin, cascadeProjection.receiverMax,
                                    cascadeProjection.receiverExtent, shadowResolution, effectiveMinCasterTexelRadius);
                                const bool isStaticCaster = shadowCaster.command->isStatic && !shadowCaster.command->jointMatrices;
                                return relevant && isStaticCaster == drawStaticCasters && (!effectiveRegion || IsBoundsOverlappingDirectionalRegion(shadowCaster.bounds, cascadeProjection.lightViewMatrix, cascadeShadowWorldOrigin, effectiveRegion->min, effectiveRegion->max));
                            },
                            [&](const ShadowCasterEntry &shadowCaster)
                            { return SelectDirectionalShadowLod(*shadowCaster.command, cascadeIndex, cascadeCount, shadowResolution); },
                            [&](const PlutoGE::render::RenderCommand &command, const glm::mat4 &model, std::size_t instanceIndex)
                            {
                                if (!passesInstanceShadowDistance(command, model, instanceIndex))
                                    return false;
                                if (!effectiveRegion || !command.mesh || command.submeshIndex >= command.mesh->GetSubmeshCount())
                                    return true;
                                const auto instanceBounds = resolveInstanceBounds(command, model, instanceIndex);
                                return IsBoundsOverlappingDirectionalRegion(
                                    instanceBounds, cascadeProjection.lightViewMatrix,
                                    cascadeShadowWorldOrigin, effectiveRegion->min, effectiveRegion->max);
                            },
                            m_shadowPassShader,
                            m_instanceBuffers[streamBufferIndex], m_instanceCapacities[streamBufferIndex],
                            m_indirectBuffers[streamBufferIndex], m_indirectCapacities[streamBufferIndex], m_indirectDrawEnabled,
                            m_indirectDrawValidated, shadowBatchInstances);
                        drawStats.submittedInstances += regionStats.submittedInstances;
                        drawStats.submittedBatches += regionStats.submittedBatches;
                        drawStats.submittedTriangles += regionStats.submittedTriangles;
                        drawStats.materialGroups += regionStats.materialGroups;
                        drawStats.apiDrawCalls += regionStats.apiDrawCalls;
                        updatedPixels += drawWidth * drawHeight;
                        Graphics::Disable(GL_SCISSOR_TEST);
                    };

                    if (staticNeedsFullRefresh)
                    {
                        drawRegion(0, 0, shadowResolution, shadowResolution, nullptr, true, true);
                        light->staticShadowCascadeValid[cascadeIndex] = true;
                    }
                    else if (requiresScrollCopy)
                    {
                        if (cascadeScroll.x != 0)
                        {
                            const int width = std::abs(cascadeScroll.x);
                            const int x = cascadeScroll.x > 0 ? 0 : shadowResolution - width;
                            const auto exposedRegion = lightSpaceRegionForPixels(x, 0, width, shadowResolution);
                            drawRegion(x, 0, width, shadowResolution, &exposedRegion, false, true);
                        }
                        if (cascadeScroll.y != 0)
                        {
                            const int height = std::abs(cascadeScroll.y);
                            const int y = cascadeScroll.y > 0 ? 0 : shadowResolution - height;
                            const auto exposedRegion = lightSpaceRegionForPixels(0, y, shadowResolution, height);
                            drawRegion(0, y, shadowResolution, height, &exposedRegion, false, true);
                        }
                        // The scratch target now contains the complete shifted
                        // cache plus the newly exposed strips. Promote it by
                        // swapping ownership instead of copying a full depth
                        // texture back to the old static allocation.
                        std::swap(light->staticShadowCascadeMaps[cascadeIndex],
                                  light->staticShadowCascadeScratchMaps[cascadeIndex][activeScratchIndex]);
                        light->nextStaticShadowScratchIndex[cascadeIndex] =
                            static_cast<std::uint8_t>((activeScratchIndex + 1u) % 2u);
                        staticCascadeMap = light->staticShadowCascadeMaps[cascadeIndex].get();
                    }

                    // When the stabilized cascade has not scrolled, only the
                    // pixels touched by the previous or current bounds of a
                    // moving caster can contain stale dynamic depth. Restore
                    // that conservative rectangle from the immutable static
                    // cache instead of copying the complete cascade. Any
                    // unmoved dynamic caster intersecting the rectangle is
                    // included by drawRegion below, so overlapping shadows are
                    // reconstructed exactly.
                    const auto movedCasterDirtyRegions = BuildMovedCasterDirtyRegions(
                        shadowCasters,
                        cascadeProjection,
                        cascadeShadowWorldOrigin,
                        shadowResolution,
                        glm::max(light->directionalShadowSettings.softness, 0.0f));
                    auto dynamicRefreshRegions = movedCasterDirtyRegions;
                    if (requiresScrollCopy)
                    {
                        if (cascadeScroll.x != 0)
                        {
                            const int width = std::abs(cascadeScroll.x);
                            const int x = cascadeScroll.x > 0 ? 0 : shadowResolution - width;
                            dynamicRefreshRegions.push_back(
                                lightSpaceRegionForPixels(x, 0, width, shadowResolution));
                        }
                        if (cascadeScroll.y != 0)
                        {
                            const int height = std::abs(cascadeScroll.y);
                            const int y = cascadeScroll.y > 0 ? 0 : shadowResolution - height;
                            dynamicRefreshRegions.push_back(
                                lightSpaceRegionForPixels(0, y, shadowResolution, height));
                        }
                    }

                    // Keep exposed X/Y strips independent. Their small corner
                    // overlap is cheaper than replacing the resulting L-shape
                    // with its bounding rectangle, which would cover the whole
                    // cascade whenever both axes scroll. Moving-caster regions
                    // are already clustered by BuildMovedCasterDirtyRegions.
                    const std::int64_t cascadePixelCount =
                        static_cast<std::int64_t>(shadowResolution) * shadowResolution;
                    const std::int64_t dirtyPixelCount = std::accumulate(
                        dynamicRefreshRegions.begin(), dynamicRefreshRegions.end(), std::int64_t{0},
                        [](std::int64_t total, const DirectionalShadowDirtyRegion &region)
                        {
                            return total + static_cast<std::int64_t>(region.pixelWidth) * region.pixelHeight;
                        });
                    constexpr float kMaximumPartialDynamicUpdateCoverage = 0.6f;
                    const bool canPartiallyRestoreDynamicDepth =
                        !staticNeedsFullRefresh &&
                        cascadeScroll.valid &&
                        !dynamicRefreshRegions.empty() &&
                        static_cast<double>(dirtyPixelCount) <=
                            static_cast<double>(cascadePixelCount) * kMaximumPartialDynamicUpdateCoverage;

                    if (canPartiallyRestoreDynamicDepth)
                    {
                        if (requiresScrollCopy)
                        {
                            // The static cache has already been promoted, so its
                            // retired allocation is free to receive the shifted
                            // combined (static + dynamic) map. Promote that map
                            // too, then repair only newly exposed and caster-dirty
                            // rectangles below.
                            auto &combinedScratchMap =
                                light->staticShadowCascadeScratchMaps[cascadeIndex][activeScratchIndex];
                            const unsigned int combinedScratchTexture = combinedScratchMap->GetTextureID();
                            const unsigned int previousCombinedTexture = cascadeMap->GetTextureID();
                            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                                   combinedScratchTexture, 0);
                            Graphics::Disable(GL_SCISSOR_TEST);
                            glClear(GL_DEPTH_BUFFER_BIT);
                            const int sourceX = glm::max(0, -cascadeScroll.x);
                            const int sourceY = glm::max(0, -cascadeScroll.y);
                            const int destinationX = glm::max(0, cascadeScroll.x);
                            const int destinationY = glm::max(0, cascadeScroll.y);
                            const int copyWidth = shadowResolution - std::abs(cascadeScroll.x);
                            const int copyHeight = shadowResolution - std::abs(cascadeScroll.y);
                            glCopyImageSubData(previousCombinedTexture, GL_TEXTURE_2D, 0,
                                               sourceX, sourceY, 0,
                                               combinedScratchTexture, GL_TEXTURE_2D, 0,
                                               destinationX, destinationY, 0,
                                               copyWidth, copyHeight, 1);
                            std::swap(light->shadowCascadeMaps[cascadeIndex], combinedScratchMap);
                            cascadeMap = light->shadowCascadeMaps[cascadeIndex].get();
                        }

                        for (const auto &dirtyRegion : dynamicRefreshRegions)
                        {
                            glCopyImageSubData(
                                staticCascadeMap->GetTextureID(), GL_TEXTURE_2D, 0,
                                dirtyRegion.pixelX, dirtyRegion.pixelY, 0,
                                cascadeMap->GetTextureID(), GL_TEXTURE_2D, 0,
                                dirtyRegion.pixelX, dirtyRegion.pixelY, 0,
                                dirtyRegion.pixelWidth, dirtyRegion.pixelHeight, 1);
                        }
                    }
                    else
                    {
                        glCopyImageSubData(staticCascadeMap->GetTextureID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                                           cascadeMap->GetTextureID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                                           shadowResolution, shadowResolution, 1);
                    }
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, cascadeMap->GetTextureID(), 0);
                    if (!ValidateShadowFramebuffer("combined directional cascade shadow", cascadeMap->GetTextureID()))
                    {
                        continue;
                    }
                    if (canPartiallyRestoreDynamicDepth)
                    {
                        for (const auto &dirtyRegion : dynamicRefreshRegions)
                        {
                            drawRegion(dirtyRegion.pixelX,
                                       dirtyRegion.pixelY,
                                       dirtyRegion.pixelWidth,
                                       dirtyRegion.pixelHeight,
                                       &dirtyRegion,
                                       false,
                                       false);
                        }
                    }
                    else
                    {
                        drawRegion(0, 0, shadowResolution, shadowResolution, nullptr, false, false);
                    }
                    if (ctx.renderer)
                    {
                        ctx.renderer->RecordShadowMapUpdate(
                            updatedPixels,
                            drawStats.submittedInstances,
                            drawStats.submittedBatches,
                            drawStats.submittedTriangles,
                            drawStats.materialGroups,
                            drawStats.apiDrawCalls,
                            true);
                    }
                    light->pendingShadowCascadeMask &= static_cast<std::uint8_t>(~(1u << cascadeIndex));
                }

                for (int cascadeIndex = cascadeCount; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
                {
                    light->shadowCascadeWorldOrigins[cascadeIndex] = glm::vec3(0.0f);
                    light->shadowCascadeMatrices[cascadeIndex] = glm::mat4(1.0f);
                    light->shadowCascadeSplits[cascadeIndex] = light->shadowFarPlane;
                }

                light->isDirty = false;
                light->shadowRefreshPending = deferredShadowRefresh || light->pendingShadowCascadeMask != 0;
                Graphics::Disable(GL_POLYGON_OFFSET_FILL);
                Graphics::Enable(GL_CULL_FACE);
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
            const bool urgentMovedCasterProjectedShadow = AnyMovedShadowCasterRelevant(
                shadowCasters,
                [&](const ShadowCasterEntry &shadowCaster)
                {
                    return light->type != scene::LightType::Spot ||
                           IsMovedCommandRelevantForProjectedLight(shadowCaster, shadowFrustumPlanes);
                });
            bool projectedShadowNeedsIncrementalRefresh = hasPendingIncrementalRefresh;
            if (!light->isDirty && !projectedShadowNeedsIncrementalRefresh)
            {
                projectedShadowNeedsIncrementalRefresh = urgentMovedCasterProjectedShadow;
            }

            if (!light->isDirty)
            {
                if (!projectedShadowNeedsIncrementalRefresh)
                {
                    light->isDirty = false;
                    light->shadowRefreshPending = false;
                    continue;
                }

                if (!urgentMovedCasterProjectedShadow && !reserveIncrementalShadowSurfaceUpdate())
                {
                    light->shadowRefreshPending = true;
                    continue;
                }
            }

            light->shadowMatrix = shadowMatrix;
            light->shadowFarPlane = glm::max(light->range, 0.1f);
            Graphics::SetViewport(0, 0, shadowMap->GetWidth(), shadowMap->GetHeight());

            Graphics::Enable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
            glCullFace(GL_FRONT);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap->GetTextureID(), 0);
            if (!ValidateShadowFramebuffer("projected shadow", shadowMap->GetTextureID()))
            {
                continue;
            }
            glClear(GL_DEPTH_BUFFER_BIT);
            m_shadowPassShader->SetUniform("uShadowPassMode", kProjectedShadowPassMode);
            m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrix);
            const std::size_t streamBufferIndex = AcquireStreamBuffer();
            const ShadowDrawStats drawStats = DrawShadowCasterBatches(
                sortedShadowCasters,
                [&](const ShadowCasterEntry &shadowCaster)
                {
                    return light->type != scene::LightType::Spot ||
                           IsCommandRelevantForProjectedLight(shadowCaster, shadowFrustumPlanes);
                },
                [](const ShadowCasterEntry &shadowCaster)
                {
                    return SelectDefaultShadowLod(*shadowCaster.command);
                },
                [&](const PlutoGE::render::RenderCommand &command, const glm::mat4 &model, std::size_t instanceIndex)
                {
                    return passesInstanceShadowDistance(command, model, instanceIndex);
                },
                m_shadowPassShader,
                m_instanceBuffers[streamBufferIndex],
                m_instanceCapacities[streamBufferIndex],
                m_indirectBuffers[streamBufferIndex],
                m_indirectCapacities[streamBufferIndex],
                m_indirectDrawEnabled,
                m_indirectDrawValidated,
                shadowBatchInstances);
            if (ctx.renderer)
            {
                ctx.renderer->RecordShadowMapUpdate(
                    shadowMap->GetWidth() * shadowMap->GetHeight(),
                    drawStats.submittedInstances,
                    drawStats.submittedBatches,
                    drawStats.submittedTriangles,
                    drawStats.materialGroups,
                    drawStats.apiDrawCalls,
                    false);
            }

            light->isDirty = false;
            light->shadowRefreshPending = false;
            glCullFace(GL_BACK);
        }

        m_shadowPassShader->Unbind();
        Graphics::Disable(GL_POLYGON_OFFSET_FILL);
        glCullFace(GL_BACK);
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
        Graphics::SetViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        // Shadow maps remain conventional; restore the scene's reversed-Z state.
        glClearDepth(0.0);
        glDepthFunc(GL_GREATER);
    }
}
