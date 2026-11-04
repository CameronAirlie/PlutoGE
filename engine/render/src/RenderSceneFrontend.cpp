#include "PlutoGE/render/RenderSceneFrontend.h"

#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/core/TaskPool.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace PlutoGE::render
{
    namespace
    {
        bool HasVertexGraph(const RenderCommand &command)
        {
            return command.material && command.material->ReadConfig().shaderGraphProgram &&
                command.material->ReadConfig().shaderGraphProgram->data.header.z>0;
        }

        std::array<glm::vec4, 6> ExtractFrustumPlanes(const glm::mat4 &viewProjection)
        {
            std::array<glm::vec4, 6> planes = {
                glm::vec4(viewProjection[0][3] + viewProjection[0][0], viewProjection[1][3] + viewProjection[1][0], viewProjection[2][3] + viewProjection[2][0], viewProjection[3][3] + viewProjection[3][0]),
                glm::vec4(viewProjection[0][3] - viewProjection[0][0], viewProjection[1][3] - viewProjection[1][0], viewProjection[2][3] - viewProjection[2][0], viewProjection[3][3] - viewProjection[3][0]),
                glm::vec4(viewProjection[0][3] + viewProjection[0][1], viewProjection[1][3] + viewProjection[1][1], viewProjection[2][3] + viewProjection[2][1], viewProjection[3][3] + viewProjection[3][1]),
                glm::vec4(viewProjection[0][3] - viewProjection[0][1], viewProjection[1][3] - viewProjection[1][1], viewProjection[2][3] - viewProjection[2][1], viewProjection[3][3] - viewProjection[3][1]),
                glm::vec4(viewProjection[0][3] + viewProjection[0][2], viewProjection[1][3] + viewProjection[1][2], viewProjection[2][3] + viewProjection[2][2], viewProjection[3][3] + viewProjection[3][2]),
                glm::vec4(viewProjection[0][3] - viewProjection[0][2], viewProjection[1][3] - viewProjection[1][2], viewProjection[2][3] - viewProjection[2][2], viewProjection[3][3] - viewProjection[3][2]),
            };

            for (auto &plane : planes)
            {
                const float length = glm::length(glm::vec3(plane));
                if (length > 1e-6f)
                    plane /= length;
            }

            return planes;
        }

        float SignedDistance(const glm::vec4 &plane, const glm::vec3 &point)
        {
            return plane.x * point.x + plane.y * point.y + plane.z * point.z + plane.w;
        }

        bool PassesStaticProjectedSizeCull(
            const RenderCommand &command,
            const glm::vec3 &cameraPosition,
            float projectionScaleY,
            float halfViewportHeight)
        {
            constexpr float kStaticSubmeshPixelCullThreshold = 1.0f;
            if (!command.isStatic || kStaticSubmeshPixelCullThreshold <= 0.0f)
            {
                return true;
            }

            const glm::vec3 offset = command.worldBounds.center - cameraPosition;
            const float distanceSquared = std::max(glm::dot(offset, offset), 0.000001f);
            const float projectedRadiusNumerator = std::max(command.worldBounds.radius, 0.001f) *
                                                   projectionScaleY * halfViewportHeight;
            const float thresholdSquared = kStaticSubmeshPixelCullThreshold * kStaticSubmeshPixelCullThreshold;
            return projectedRadiusNumerator * projectedRadiusNumerator >= distanceSquared * thresholdSquared;
        }

        bool PassesBoundsDistanceCull(const MeshBounds &bounds, const glm::vec3 &cameraPosition, float maxDistance)
        {
            if (maxDistance <= 0.0f || maxDistance == std::numeric_limits<float>::max())
            {
                return true;
            }
            const float maxCenterDistance = maxDistance + std::max(bounds.radius, 0.0f);
            const glm::vec3 offset = bounds.center - cameraPosition;
            return glm::dot(offset, offset) <= maxCenterDistance * maxCenterDistance;
        }

        bool IsBoundsFullyInsideDistanceCull(const MeshBounds &bounds, const glm::vec3 &cameraPosition, float maxDistance)
        {
            if (maxDistance <= 0.0f || maxDistance == std::numeric_limits<float>::max())
            {
                return true;
            }
            const float maximumCenterDistance = std::max(maxDistance - std::max(bounds.radius, 0.0f), 0.0f);
            const glm::vec3 offset = bounds.center - cameraPosition;
            return glm::dot(offset, offset) <= maximumCenterDistance * maximumCenterDistance;
        }

        MeshBounds TransformBounds(const MeshBounds &bounds, const glm::mat4 &model)
        {
            const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(bounds.center, 1.0f));
            const float scaleX = glm::length(glm::vec3(model[0]));
            const float scaleY = glm::length(glm::vec3(model[1]));
            const float scaleZ = glm::length(glm::vec3(model[2]));
            return MeshBounds{
                .center = worldCenter,
                .radius = bounds.radius * std::max(scaleX, std::max(scaleY, scaleZ)),
            };
        }

        bool IsBoundsVisible(const MeshBounds &bounds, const std::array<glm::vec4, 6> &planes)
        {
            return std::none_of(planes.begin(), planes.end(), [&](const glm::vec4 &plane)
            {
                return SignedDistance(plane, bounds.center) < -bounds.radius;
            });
        }

        enum class FrustumContainment
        {
            Outside,
            Intersecting,
            Inside,
        };

        FrustumContainment ClassifyBounds(const MeshBounds &bounds, const std::array<glm::vec4, 6> &planes)
        {
            bool intersects = false;
            for (const auto &plane : planes)
            {
                const float signedDistance = SignedDistance(plane, bounds.center);
                if (signedDistance < -bounds.radius)
                {
                    return FrustumContainment::Outside;
                }
                intersects |= signedDistance < bounds.radius;
            }
            return intersects ? FrustumContainment::Intersecting : FrustumContainment::Inside;
        }

        // Producer-retained instance arrays (foliage clusters, whose identity and revision vouch for them) reach the RHI
        // renderer whole once their cluster is visible: it culls and bins their instances itself, keeping their records
        // and parameter blocks resident while the array is the same one. A culled copy per frame would move both every
        // frame, re-uploading records and minting cached uniform blocks until the per-frame uniform arena overflows.
        bool RetainsInstances(const RenderCommand &command)
        {
            return command.instanceModels && command.sourceObject != 0 && command.sourceRevision != 0;
        }

        RenderCommand CullRenderCommandInstances(
            const RenderCommand &command,
            const glm::vec3 &cameraPosition,
            float maxDistance,
            const std::array<glm::vec4, 6> &frustumPlanes,
            bool fullyInsideFrustum,
            const std::shared_ptr<std::vector<glm::mat4>> &modelScratch,
            const std::shared_ptr<std::vector<glm::mat4>> &previousModelScratch)
        {
            if (!command.mesh || !command.instanceModels || command.instanceModels->empty() ||
                command.submeshIndex >= command.mesh->GetSubmeshCount())
            {
                return command;
            }

            // Foliage and other static instance producers submit spatially coherent
            // clusters with conservative aggregate bounds. Most clusters are wholly
            // inside the camera volume, so avoid transforming and testing every
            // member unless the aggregate sphere crosses a culling boundary.
            if (fullyInsideFrustum &&
                IsBoundsFullyInsideDistanceCull(command.worldBounds, cameraPosition, maxDistance))
            {
                return command;
            }

            const auto &sourceBounds = command.mesh->GetSubmesh(command.submeshIndex).bounds;
            bool hasRejectedInstance = false;

            for (std::size_t index = 0; index < command.instanceModels->size(); ++index)
            {
                const glm::mat4 &model = (*command.instanceModels)[index];
                const MeshBounds instanceBounds = TransformBounds(sourceBounds, model);
                if (!IsBoundsVisible(instanceBounds, frustumPlanes) ||
                    !PassesBoundsDistanceCull(instanceBounds, cameraPosition, maxDistance))
                {
                    if (!hasRejectedInstance)
                    {
                        hasRejectedInstance = true;
                        modelScratch->clear();
                        modelScratch->reserve(command.instanceModels->size());
                        previousModelScratch->clear();
                        if (command.previousInstanceModels)
                            previousModelScratch->reserve(command.instanceModels->size());
                        for (std::size_t prefixIndex = 0; prefixIndex < index; ++prefixIndex)
                        {
                            modelScratch->push_back((*command.instanceModels)[prefixIndex]);
                            if (command.previousInstanceModels)
                                previousModelScratch->push_back(prefixIndex < command.previousInstanceModels->size()
                                                                    ? (*command.previousInstanceModels)[prefixIndex]
                                                                    : (*command.instanceModels)[prefixIndex]);
                        }
                    }
                    continue;
                }

                if (hasRejectedInstance)
                {
                    modelScratch->push_back(model);
                    if (command.previousInstanceModels)
                        previousModelScratch->push_back(index < command.previousInstanceModels->size()
                                                            ? (*command.previousInstanceModels)[index]
                                                            : model);
                }
            }

            if (!hasRejectedInstance)
            {
                return command;
            }

            RenderCommand culled = command;
            culled.instanceModels = modelScratch;
            culled.previousInstanceModels = command.previousInstanceModels ? previousModelScratch : nullptr;
            // The scratch arrays are pooled by visible order and refilled every
            // frame, so the producer revision no longer vouches for the contents:
            // a cached packet would draw this mesh with another cluster's models.
            culled.sourceRevision = 0;
            return culled;
        }

        auto RenderCommandKey(const RenderCommand &command)
        {
            const auto range = command.mesh
                ? command.mesh->GetSubmeshLodRange(command.submeshIndex, command.lodIndex) : Submesh::LodRange{};
            // Materials sharing a shader graph share pipelines, so group them first.
            const auto *graph = command.material ? command.material->ReadConfig().shaderGraphProgram.get() : nullptr;
            return std::tuple{reinterpret_cast<std::uintptr_t>(graph),
                reinterpret_cast<std::uintptr_t>(command.material), reinterpret_cast<std::uintptr_t>(command.mesh),
                range.indexOffset, range.indexCount, command.submeshIndex};
        }

        // Orders commands by draw state so renderers batch consecutive draws.
        template<typename Reference>
        void SortByDrawState(std::vector<Reference> &references)
        {
            // Resolve material and mesh metadata once, not at every comparison.
            using Key = decltype(RenderCommandKey(RenderCommand{}));
            std::vector<Key> keys;
            keys.reserve(references.size());
            for (const auto *command : references) keys.push_back(RenderCommandKey(*command));
            std::vector<std::size_t> permutation(references.size());
            std::iota(permutation.begin(), permutation.end(), std::size_t{0});
            std::sort(permutation.begin(), permutation.end(), [&](std::size_t a, std::size_t b) {
                return keys[a] < keys[b];
            });
            std::vector<Reference> sorted;
            sorted.reserve(permutation.size());
            for (const auto index : permutation) sorted.push_back(references[index]);
            references.swap(sorted);
        }
    }

    void RenderSceneFrontend::ClearRenderCommands()
    {
        m_sceneRefsDirty |= !m_transientCommands.empty();
        m_retainedCounted = false;
        m_retainedScene.BeginFrame();
        m_visibleCommandRefs.clear();
        m_transientCommands.clear();
        m_decalCommands.clear();
        ClearSubmissionCullingCameras();
    }

    void RenderSceneFrontend::AppendSubmittedCommand(const RenderCommand &command)
    {
        auto &submitted = m_transientCommands.emplace_back(command);
        if (submitted.mesh && submitted.material && submitted.material->ReadConfig().shaderGraphProgram)
            submitted.mesh = submitted.mesh->GetTessellated(unsigned(submitted.material->ReadConfig().shaderGraphProgram->data.header.w));
        ++m_stats.submittedRenderCommandCount;
        if (command.material)
            for (const auto &pass : command.material->ReadConfig().additionalPasses)
            {
                auto overlay = command;
                overlay.material = pass.get();
                overlay.castsShadow = false;
                SubmitRenderCommand(overlay);
            }
    }

    void RenderSceneFrontend::SubmitRenderCommand(const RenderCommand &command)
    {
        m_sceneRefsDirty = true;
        if (!IsRenderCommandAcceptedForSubmission(command))
        {
            ++m_stats.submissionCulledRenderCommandCount;
            return;
        }
        AppendSubmittedCommand(command);
    }

    void RenderSceneFrontend::SubmitSortedRenderCommands(const std::vector<RenderCommand> &commands,
                                                         bool applySubmissionCulling)
    {
        if (commands.empty())
            return;
        m_sceneRefsDirty = true;
        m_transientCommands.reserve(m_transientCommands.size() + commands.size());
        for (const auto &command : commands)
        {
            if (applySubmissionCulling && !IsRenderCommandAcceptedForSubmission(command))
            {
                ++m_stats.submissionCulledRenderCommandCount;
                continue;
            }
            AppendSubmittedCommand(command);
        }
    }

    void RenderSceneFrontend::SetSubmissionCullingCameras(const std::vector<CameraData> &cameraDatas)
    {
        m_submissionFrustums.clear();
        m_submissionFrustums.reserve(cameraDatas.size());
        for (const auto &cameraData : cameraDatas)
            m_submissionFrustums.push_back(ExtractFrustumPlanes(cameraData.projection * cameraData.view));
    }

    bool RenderSceneFrontend::IsRenderCommandAcceptedForSubmission(const RenderCommand &command) const
    {
        if (HasVertexGraph(command))
            return true;
        if (m_submissionFrustums.empty())
            return true;

        // An object outside every camera view can still lie inside a light's
        // shadow frustum and cast onto visible geometry, so shadow casters are
        // retained regardless of camera visibility.
        if (command.castsShadow && command.material && command.material->ReadConfig().castsShadow)
            return true;
        // Global illumination (voxel and ray-traced) reads the static scene from this list, off screen as well.
        // Culling it by the camera would leave out what is behind the view and change GI's scene on every turn.
        if (command.isStatic)
            return true;

        return std::any_of(m_submissionFrustums.begin(), m_submissionFrustums.end(), [&](const auto &planes)
        {
            return IsBoundsVisible(command.worldBounds, planes);
        });
    }

    void RenderSceneFrontend::SynchronizeRenderScene()
    {
        m_retainedScene.Synchronize();
        const auto &retainedStats = m_retainedScene.GetStats();
        m_stats.retainedProducers = retainedStats.activeProducers;
        m_stats.retainedCommands = retainedStats.activeCommands;
        m_stats.retainedUpdates = retainedStats.updates;
        m_stats.retainedReuses = retainedStats.reusedProducers;
        m_stats.retainedRemovals = retainedStats.removedProducers;
        m_stats.retainedCommandRebuilds = retainedStats.rebuiltCommands;
        if (m_sceneRefsDirty || m_retainedSceneRevision != m_retainedScene.Revision())
        {
            m_sceneCommandRefs.assign(m_retainedScene.Commands().begin(), m_retainedScene.Commands().end());
            for (auto &command : m_transientCommands) m_sceneCommandRefs.push_back(&command);
            m_retainedSceneRevision = m_retainedScene.Revision();
            m_sceneRefsDirty = false;
            m_sceneSortDirty = true;
        }
        if (!m_retainedCounted)
        {
            m_stats.submittedRenderCommandCount += static_cast<int>(m_retainedScene.Commands().size());
            m_retainedCounted = true;
        }
    }

    RenderCommandView RenderSceneFrontend::GetSceneRenderCommandView()
    {
        SynchronizeRenderScene();
        return RenderCommandView(m_sceneCommandRefs);
    }

    const std::vector<RenderCommand> &RenderSceneFrontend::GetSceneRenderCommands()
    {
        const auto view = GetSceneRenderCommandView();
        m_sceneSnapshot.assign(view.begin(), view.end());
        return m_sceneSnapshot;
    }

    const std::vector<RenderCommand> &RenderSceneFrontend::GetVisibleRenderCommands() const
    {
        const auto view = GetVisibleRenderCommandView();
        m_visibleSnapshot.assign(view.begin(), view.end());
        return m_visibleSnapshot;
    }

    void RenderSceneFrontend::UpdateRenderCommandLods(const CameraData &cameraData, int viewportHeight,
                                                      std::span<RenderCommand *const> commands)
    {
        const glm::mat4 inverseView = glm::inverse(cameraData.view);
        const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
        const float projectionScaleY = std::abs(cameraData.projection[1][1]);
        const float halfViewportHeight = static_cast<float>(std::max(viewportHeight, 1)) * 0.5f;
        const auto frustumPlanes = ExtractFrustumPlanes(cameraData.projection * cameraData.view);
        m_visibilityCandidates.clear();
        m_visibilityCandidates.reserve(commands.size());

        // Selects one command's LOD and tests its visibility. Touches only that command, so ranges of commands run on
        // the task pool, each collecting its own candidates; concatenating them in range order gives the serial order.
        const auto classify = [&](std::size_t commandIndex, std::vector<std::pair<std::size_t, bool>> &candidates) -> bool
        {
            bool changed = false;
            auto &command = *commands[commandIndex];
            if (!command.mesh)
            {
                return false;
            }

            if (command.mesh->GetSubmeshLodCount(command.submeshIndex) > 1)
            {
                const glm::vec3 cameraOffset = command.worldBounds.center - cameraPosition;
                const float safeDistance = std::sqrt(std::max(glm::dot(cameraOffset, cameraOffset), 0.000001f));
                const float projectedRadiusPixels = (std::max(command.worldBounds.radius, 0.001f) / safeDistance) * projectionScaleY * halfViewportHeight;
                const auto selection = command.mesh->SelectSubmeshLodWithTransition(
                    command.submeshIndex, projectedRadiusPixels, command.GetMinLodIndex());
                if (command.lodIndex != selection.index ||
                    command.GetLodTransitionIndex() != selection.transitionIndex ||
                    std::abs(command.GetLodTransitionFade() - selection.fade) > (1.0f / 65535.0f))
                {
                    // Fade/history bits are not sort keys. Only a new index
                    // range can disturb the already sorted command order.
                    changed |= command.lodIndex != selection.index;
                    command.lodIndex = selection.index;
                    command.SetLodTransition(selection.transitionIndex, selection.fade);
                }
            }

            if (HasVertexGraph(command))
            {
                candidates.emplace_back(commandIndex, false);
                return changed;
            }
            const FrustumContainment containment = ClassifyBounds(command.worldBounds, frustumPlanes);
            if (containment != FrustumContainment::Outside &&
                PassesBoundsDistanceCull(command.worldBounds, cameraPosition, command.maxDrawDistance) &&
                PassesStaticProjectedSizeCull(command, cameraPosition, projectionScaleY, halfViewportHeight))
            {
                candidates.emplace_back(commandIndex, containment == FrustumContainment::Inside);
            }
            return changed;
        };

        bool changed = false;
        constexpr std::size_t commandsPerRange = 256;
        auto &pool = core::TaskPool::Shared();
        const auto ranges = m_parallelVisibility ? pool.RangeCount(commands.size(), commandsPerRange) : 1;
        if (ranges <= 1)
        {
            for (std::size_t commandIndex = 0; commandIndex < commands.size(); ++commandIndex)
                changed |= classify(commandIndex, m_visibilityCandidates);
        }
        else
        {
            m_visibilityCandidateRanges.resize(ranges);
            std::vector<char> rangeChanged(ranges, 0);
            pool.RunRanges(commands.size(), commandsPerRange, [&](std::size_t range, std::size_t begin, std::size_t end)
            {
                auto &candidates = m_visibilityCandidateRanges[range];
                candidates.clear();
                for (std::size_t commandIndex = begin; commandIndex < end; ++commandIndex)
                    rangeChanged[range] |= classify(commandIndex, candidates) ? 1 : 0;
            });
            for (std::size_t range = 0; range < ranges; ++range)
            {
                changed |= rangeChanged[range] != 0;
                m_visibilityCandidates.insert(m_visibilityCandidates.end(), m_visibilityCandidateRanges[range].begin(),
                                              m_visibilityCandidateRanges[range].end());
            }
        }

        if (changed)
            m_sceneSortDirty = true;
    }

    void RenderSceneFrontend::PrepareVisibleRenderCommands(const CameraData &cameraData, int viewportHeight)
    {
        core::CpuScope scope("Viewport visibility preparation", core::CpuCategory::Rendering);
        const auto preparationStart = std::chrono::high_resolution_clock::now();
        SynchronizeRenderScene();
        if (m_sceneSortDirty)
        {
            SortByDrawState(m_sceneCommandRefs);
            m_sceneSortDirty = false;
            ++m_stats.renderCommandSortCount;
        }
        const auto sortEnd = std::chrono::high_resolution_clock::now();
        UpdateRenderCommandLods(cameraData, viewportHeight, m_sceneCommandRefs);
        m_visibleCommandRefs.clear();
        m_visibleCommandRefs.reserve(m_sceneCommandRefs.size());
        m_visibleRenderCommands.clear();
        m_visibleRenderCommands.reserve(m_sceneCommandRefs.size());
        const auto frustumPlanes = ExtractFrustumPlanes(cameraData.projection * cameraData.view);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        // Instanced candidates cull their instances into pooled scratch arrays, one per candidate in visible order.
        // The culling touches only its own command and scratch, so it runs across the task pool; the visible list is
        // then assembled in candidate order.
        auto &instanceCulls = m_instanceCullJobs;
        instanceCulls.clear();
        for (std::size_t candidate = 0; candidate < m_visibilityCandidates.size(); ++candidate)
        {
            const auto commandIndex = m_visibilityCandidates[candidate].first;
            if (commandIndex < m_sceneCommandRefs.size() && m_sceneCommandRefs[commandIndex]->instanceModels &&
                !RetainsInstances(*m_sceneCommandRefs[commandIndex]))
                instanceCulls.push_back({candidate, {}});
        }
        while (m_visibleInstanceModelPool.size() < instanceCulls.size())
        {
            m_visibleInstanceModelPool.push_back(std::make_shared<std::vector<glm::mat4>>());
            m_visiblePreviousInstanceModelPool.push_back(std::make_shared<std::vector<glm::mat4>>());
        }
        // Culling refills pooled arrays in place. One a frame still in flight holds (a render thread recording the
        // previous frame, or a cached packet) is replaced instead, so that frame keeps the instances it was given.
        for (std::size_t job = 0; job < instanceCulls.size(); ++job)
        {
            if (m_visibleInstanceModelPool[job].use_count() > 1)
                m_visibleInstanceModelPool[job] = std::make_shared<std::vector<glm::mat4>>();
            if (m_visiblePreviousInstanceModelPool[job].use_count() > 1)
                m_visiblePreviousInstanceModelPool[job] = std::make_shared<std::vector<glm::mat4>>();
        }
        const auto cull = [&](std::size_t job)
        {
            const auto &[commandIndex, fullyInsideFrustum] = m_visibilityCandidates[instanceCulls[job].candidate];
            const auto &command = *m_sceneCommandRefs[commandIndex];
            instanceCulls[job].visible = CullRenderCommandInstances(
                command, cameraPosition, command.maxDrawDistance, frustumPlanes, fullyInsideFrustum,
                m_visibleInstanceModelPool[job], m_visiblePreviousInstanceModelPool[job]);
        };
        if (m_parallelVisibility && instanceCulls.size() > 1)
            core::TaskPool::Shared().Run(instanceCulls.size(), cull);
        else
            for (std::size_t job = 0; job < instanceCulls.size(); ++job)
                cull(job);
        std::size_t nextCull = 0;
        for (const auto &[commandIndex, fullyInsideFrustum] : m_visibilityCandidates)
        {
            if (commandIndex >= m_sceneCommandRefs.size())
                continue;
            const auto &command = *m_sceneCommandRefs[commandIndex];
            if (!command.instanceModels || RetainsInstances(command))
            {
                m_visibleCommandRefs.push_back(&command);
                continue;
            }
            auto &visibleCommand = instanceCulls[nextCull++].visible;
            if (visibleCommand.instanceModels && visibleCommand.instanceModels->empty())
                continue;
            m_visibleRenderCommands.push_back(std::move(visibleCommand));
            m_visibleCommandRefs.push_back(&m_visibleRenderCommands.back());
        }
        // A LOD change re-sorts the scene next frame; keep this frame's visible list in draw order meanwhile.
        if (m_sceneSortDirty)
        {
            SortByDrawState(m_visibleCommandRefs);
            ++m_stats.renderCommandSortCount;
        }

        const auto preparationEnd = std::chrono::high_resolution_clock::now();
        m_stats.commandSortMs += std::chrono::duration<float, std::milli>(sortEnd - preparationStart).count();
        m_stats.visibilityMs += std::chrono::duration<float, std::milli>(preparationEnd - sortEnd).count();
        m_stats.visibleRenderCommandCount = static_cast<int>(m_visibleCommandRefs.size());
        m_stats.frustumCulledRenderCommandCount =
            static_cast<int>(m_sceneCommandRefs.size() - m_visibleCommandRefs.size());
        m_stats.visibleSingleLodCommandCount = 0;
        m_stats.visibleMultiLodCommandCount = 0;
        for (const auto *command : m_visibleCommandRefs)
        {
            if (command->mesh && command->mesh->GetSubmeshLodCount(command->submeshIndex) > 1)
                ++m_stats.visibleMultiLodCommandCount;
            else
                ++m_stats.visibleSingleLodCommandCount;
        }
    }
}
