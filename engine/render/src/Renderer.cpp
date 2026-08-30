#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/passes/DecalPass.h"
#include "PlutoGE/render/passes/GridPass.h"
#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/render/passes/LightPropagationVolumePass.h"
#include "PlutoGE/render/passes/OceanPass.h"
#include "PlutoGE/render/passes/PostProcessPass.h"
#include "PlutoGE/render/passes/ParticlePass.h"
#include "PlutoGE/render/passes/PhysicalSkyPass.h"
#include "PlutoGE/render/passes/RuntimeUIPass.h"
#include "PlutoGE/render/passes/RmlUiPass.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/passes/ShadowPass.h"
#include "PlutoGE/render/passes/TransparentPass.h"
#include "PlutoGE/render/passes/VolumetricCloudPass.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/SSAOEffect.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <string_view>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kNanosecondsToMilliseconds = 1.0f / 1000000.0f;
        constexpr std::size_t kLightingSetupStage = 0;
        constexpr std::size_t kLightingAmbientStage = 1;
        constexpr std::size_t kLightingAccumulationStage = 2;

        void HashShadowCommandValue(std::uint64_t &seed, std::uint64_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }

        bool IsShadowCasterCommand(const RenderCommand &command)
        {
            return command.mesh && command.material && command.castsShadow &&
                   command.material->GetConfig().castsShadow &&
                   command.material->GetConfig().alphaMode != AlphaMode::Blend;
        }

        bool ShadowTransformsEqual(const glm::mat4 &a, const glm::mat4 &b,
                                   float epsilon = 0.0001f)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (std::abs(a[column][row] - b[column][row]) > epsilon)
                        return false;
            return true;
        }

        struct FrustumPlane
        {
            glm::vec3 normal{0.0f};
            float distance = 0.0f;
        };

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

        bool PassesDistanceCull(const RenderCommand &command, const glm::vec3 &cameraPosition, float maxDistance)
        {
            if (maxDistance <= 0.0f || maxDistance == std::numeric_limits<float>::max())
            {
                return true;
            }

            const float radius = std::max(command.worldBounds.radius, 0.0f);
            const float maximumCenterDistance = maxDistance + radius;
            const glm::vec3 offset = command.worldBounds.center - cameraPosition;
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

        bool IsBoundsVisible(const MeshBounds &bounds, const std::array<FrustumPlane, 6> &planes)
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

        enum class FrustumContainment
        {
            Outside,
            Intersecting,
            Inside,
        };

        FrustumContainment ClassifyBounds(const MeshBounds &bounds, const std::array<FrustumPlane, 6> &planes)
        {
            bool intersects = false;
            for (const auto &plane : planes)
            {
                const float signedDistance = glm::dot(plane.normal, bounds.center) + plane.distance;
                if (signedDistance < -bounds.radius)
                {
                    return FrustumContainment::Outside;
                }
                intersects |= signedDistance < bounds.radius;
            }
            return intersects ? FrustumContainment::Intersecting : FrustumContainment::Inside;
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

        RenderCommand CullRenderCommandInstances(
            const RenderCommand &command,
            const glm::vec3 &cameraPosition,
            float maxDistance,
            const std::array<FrustumPlane, 6> *frustumPlanes,
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
                if ((frustumPlanes && !IsBoundsVisible(instanceBounds, *frustumPlanes)) ||
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
            return culled;
        }

        Shader *GetRenderCommandShaderKey(const RenderCommand &command)
        {
            return command.material ? command.material->GetShader() : command.shader;
        }

        bool CompareRenderCommandKeysImpl(const RenderCommand &a, const RenderCommand &b)
        {
            const auto *aShader = GetRenderCommandShaderKey(a);
            const auto *bShader = GetRenderCommandShaderKey(b);
            if (aShader != bShader)
            {
                return aShader < bShader;
            }

            if (a.material != b.material)
            {
                return a.material < b.material;
            }

            if (a.mesh != b.mesh)
            {
                return a.mesh < b.mesh;
            }

            const auto aRange = a.mesh ? a.mesh->GetSubmeshLodRange(a.submeshIndex, a.lodIndex) : Submesh::LodRange{};
            const auto bRange = b.mesh ? b.mesh->GetSubmeshLodRange(b.submeshIndex, b.lodIndex) : Submesh::LodRange{};
            if (aRange.indexOffset != bRange.indexOffset)
            {
                return aRange.indexOffset < bRange.indexOffset;
            }

            if (aRange.indexCount != bRange.indexCount)
            {
                return aRange.indexCount < bRange.indexCount;
            }

            if (a.submeshIndex != b.submeshIndex)
            {
                return a.submeshIndex < b.submeshIndex;
            }

            return false;
        }

        std::uint64_t RenderCommandSortIdentity(const RenderCommand &command)
        {
            std::uint64_t identity = 1469598103934665603ull;
            const auto combine = [&identity](std::uint64_t value)
            {
                identity ^= value + 0x9e3779b97f4a7c15ull + (identity << 6u) + (identity >> 2u);
            };
            combine(reinterpret_cast<std::uintptr_t>(GetRenderCommandShaderKey(command)));
            combine(reinterpret_cast<std::uintptr_t>(command.material));
            combine(reinterpret_cast<std::uintptr_t>(command.mesh));
            combine(command.submeshIndex);
            combine(command.lodIndex);
            if (command.mesh)
            {
                const auto range = command.mesh->GetSubmeshLodRange(command.submeshIndex, command.lodIndex);
                combine(range.indexOffset);
                combine(range.indexCount);
            }
            return identity;
        }

        glm::vec4 ToSubmissionPlane(const FrustumPlane &plane)
        {
            return glm::vec4(plane.normal, plane.distance);
        }

        bool EnsureRenderTargetSize(RenderTarget *renderTarget, int width, int height)
        {
            if (!renderTarget)
            {
                return false;
            }

            if (renderTarget->GetWidth() == width && renderTarget->GetHeight() == height && renderTarget->IsInitialized())
            {
                return true;
            }

            return renderTarget->Resize(width, height);
        }

        void BlitColorBuffer(RenderTarget *source, RenderTarget *destination)
        {
            if (!source || !destination)
            {
                return;
            }

            Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebufferID());
            Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, destination->GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source->GetWidth(), source->GetHeight(),
                0, 0, destination->GetWidth(), destination->GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);
            Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        TAAEffect *FindActiveTAAEffect(const std::vector<IPostProcessEffect *> *postProcessEffects)
        {
            if (!postProcessEffects)
            {
                return nullptr;
            }

            for (auto *effect : *postProcessEffects)
            {
                if (!effect || !effect->IsEnabled())
                {
                    continue;
                }

                if (auto *taaEffect = dynamic_cast<TAAEffect *>(effect))
                {
                    return taaEffect;
                }
            }

            return nullptr;
        }

        void ApplyAmbientOcclusionBeforeVolumetrics(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects || !ctx.temporaryRenderTarget || !ctx.postProcessIntermediateRenderTarget)
            {
                return;
            }

            for (auto *effect : *ctx.postProcessEffects)
            {
                auto *ssaoEffect = dynamic_cast<SSAOEffect *>(effect);
                if (!ssaoEffect || !ssaoEffect->IsEnabled())
                {
                    continue;
                }

                const bool gpuTimingActive = ctx.renderer && ctx.renderer->BeginPostProcessEffectTiming(ssaoEffect->GetTypeName());
                ssaoEffect->Apply(PostProcessContext{
                    .renderContext = ctx,
                    .sourceRenderTarget = ctx.temporaryRenderTarget,
                    .destinationRenderTarget = ctx.postProcessIntermediateRenderTarget,
                });
                if (gpuTimingActive)
                {
                    ctx.renderer->EndPostProcessEffectTiming();
                }

                BlitColorBuffer(ctx.postProcessIntermediateRenderTarget, ctx.temporaryRenderTarget);
            }
        }

        void ExpandCaptureFarPlaneForClouds(const scene::Entity *entity,
                                            const glm::vec3 &capturePosition,
                                            float &farPlane)
        {
            if (!entity || !entity->IsActive())
                return;

            for (const auto *cloud : entity->GetComponents<scene::VolumetricCloudComponent>())
            {
                if (!cloud || !cloud->IsEnabled() || cloud->GetDensity() <= 0.0f || cloud->GetCoverage() <= 0.0f)
                    continue;

                const glm::vec3 scaledSize = glm::abs(cloud->GetSize() * entity->GetWorldScale());
                const float boundingRadius = glm::length(scaledSize) * 0.5f;
                const float requiredDistance = glm::length(entity->GetWorldPosition() - capturePosition) + boundingRadius;
                farPlane = std::max(farPlane, requiredDistance + 1.0f);
            }

            for (const auto *child : entity->GetChildren())
                ExpandCaptureFarPlaneForClouds(child, capturePosition, farPlane);
        }

        float ResolveEnvironmentCaptureFarPlane(const scene::Scene *scene,
                                                const glm::vec3 &capturePosition,
                                                float requestedFarPlane)
        {
            float resolvedFarPlane = std::max(requestedFarPlane, 1.0f);
            if (!scene)
                return resolvedFarPlane;
            for (const auto *root : scene->GetRootEntities())
                ExpandCaptureFarPlaneForClouds(root, capturePosition, resolvedFarPlane);
            return resolvedFarPlane;
        }
    }

    void ResizeCallback(int width, int height)
    {
        Graphics::SetViewport(0, 0, width, height);
    }

    bool Renderer::Initialize(const RendererConfig &config)
    {
        m_config = config;

        auto window = m_config.window;
        if (!window)
        {
            // Handle error: window pointer is null
            return false;
        }

        window->SetResizeCallback(ResizeCallback);

        if (!window->EnsureOpenGLContextCurrent(true))
        {
            std::cerr << "Failed to prepare the required OpenGL 4.3 context and function dispatch." << std::endl;
            return false;
        }

        auto extents = window->GetExtents();
        Graphics::SetViewport(0, 0, extents.width, extents.height);

        Graphics::Enable(GL_DEPTH_TEST);
        glClearDepth(0.0);
        glDepthFunc(GL_GREATER);
        Graphics::Enable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        auto geometryPass = new GeometryPass();
        geometryPass->Initialize();
        m_renderPasses.push_back(geometryPass);

        auto decalPass = new DecalPass();
        decalPass->Initialize();
        m_renderPasses.push_back(decalPass);

        auto shadowPass = new ShadowPass();
        shadowPass->Initialize();
        m_shadowPass = shadowPass;

        auto lightPropagationVolumePass = new LightPropagationVolumePass();
        lightPropagationVolumePass->Initialize();
        m_lightPropagationVolumePass = lightPropagationVolumePass;
        m_renderPasses.push_back(lightPropagationVolumePass);

        auto lightingPass = new LightingPass();
        lightingPass->Initialize();
        m_renderPasses.push_back(lightingPass);

        auto physicalSkyPass = new PhysicalSkyPass();
        physicalSkyPass->Initialize();
        m_physicalSkyPass = physicalSkyPass;
        m_renderPasses.push_back(physicalSkyPass);

        auto gridPass = new GridPass();
        gridPass->Initialize();
        m_renderPasses.push_back(gridPass);

        auto oceanPass = new OceanPass();
        oceanPass->Initialize();
        m_renderPasses.push_back(oceanPass);

        auto volumetricCloudPass = new VolumetricCloudPass();
        volumetricCloudPass->Initialize();
        m_renderPasses.push_back(volumetricCloudPass);

        auto particlePass = new ParticlePass();
        particlePass->Initialize();
        m_renderPasses.push_back(particlePass);

        auto postProcessPass = new PostProcessPass();
        postProcessPass->Initialize();
        m_renderPasses.push_back(postProcessPass);

        auto transparentPass = new TransparentPass();
        transparentPass->Initialize();
        m_renderPasses.push_back(transparentPass);

        auto runtimeUIPass = new RuntimeUIPass();
        runtimeUIPass->Initialize();
        m_renderPasses.push_back(runtimeUIPass);

        auto rmlUiPass = new RmlUiPass();
        rmlUiPass->Initialize();
        m_renderPasses.push_back(rmlUiPass);

        InitializeGpuTimers();

        if (!GetOrCreateFrameResources(nullptr, extents.width, extents.height))
        {
            return false;
        }

        m_isInitialized = true;
        return true;
    }

    void Renderer::BeginFrame(RenderTarget *renderTarget)
    {
        if (!m_isInitialized)
            return;

        if (!m_config.window || !m_config.window->EnsureOpenGLContextCurrent())
        {
            return;
        }

        ++m_frameSequence;

        if (renderTarget)
        {
            Graphics::ClearRenderTarget(renderTarget);
            return;
        }

        if (m_config.window)
        {
            const auto extents = m_config.window->GetExtents();
            Graphics::SetViewport(0, 0, extents.width, extents.height);
        }

        Graphics::ClearRenderTarget(nullptr);
    }

    void Renderer::BeginProfilingFrame()
    {
        if (!m_config.enableProfiling)
            return;

        for (auto &cpuPassTiming : m_cpuPassTimings)
        {
            cpuPassTiming.cpuTimeMs = 0.0f;
        }

        ResolveAllGpuTimings();
        ResolveAllLightingGpuTimings();
        ResolveAllPostProcessGpuTimings();
        ResolveAllGpuDetailTimings();

        // Effect query objects persist so their asynchronous results can be
        // reused, but visibility in the profiler is frame-local. An effect
        // that is not submitted again this frame must not retain a stale
        // timing (or contribute it to the Post Process total).
        for (auto &postProcessGpuTiming : m_postProcessGpuTimings)
        {
            postProcessGpuTiming.hasResult = false;
        }
        UpdatePostProcessGpuTimingTotal();
        m_gpuTimingsResolvedThisFrame = true;

        m_cpuFrameStats = {};
        m_profiledRenderCount = 0;
    }

    void Renderer::UpdateShadowMaps(std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized || !m_shadowPass)
            return;

        if (!m_config.window || !m_config.window->EnsureOpenGLContextCurrent())
        {
            return;
        }

        Shader::ResetStateCache();
        Graphics::ResetStateCache();

        if (!m_config.window)
        {
            return;
        }

        const auto extents = m_config.window->GetExtents();
        auto *frameResources = GetOrCreateFrameResources(nullptr, extents.width, extents.height);
        if (!frameResources)
        {
            return;
        }

        EnsureRenderCommandsSorted();
        FinalizeShadowCommandSummary();

        RenderContext ctx{
            .renderer = this,
            .cameraData = {},
            .previousCameraData = {},
            .hasCameraData = false,
            .hasPreviousCameraData = false,
            .cameraComponent = nullptr,
            .postProcessEffects = nullptr,
            .renderTarget = nullptr,
            .temporaryRenderTarget = frameResources->temporaryRenderTarget.get(),
            .postProcessIntermediateRenderTarget = frameResources->postProcessIntermediateRenderTarget.get(),
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &frameResources->gBuffer,
            .lightPropagationVolumePass = m_lightPropagationVolumePass,
            .postProcessDebugView = m_postProcessDebugView,
            .frameSequence = m_frameSequence,
            .oceanSurfaceDepthRenderTarget = frameResources->oceanSurfaceDepthRenderTarget.get(),
            .oceanSceneColorCopyRenderTarget = frameResources->oceanSceneColorCopyRenderTarget.get(),
            .shadowCasterCommandIndices = &m_shadowCasterCommandIndices,
            .shadowCasterFingerprint = m_shadowCasterFingerprint,
            .shadowCastersMoved = m_shadowCastersMoved,
            .allShadowCastersStatic = m_allShadowCastersStatic,
        };

        ExecutePassWithGpuTiming(*m_shadowPass, ctx, 0);
    }

    bool Renderer::CaptureSceneCubemap(const glm::vec3 &position, int resolution, float farPlane, Texture *targetCubemap, std::vector<scene::Light *> lights, const scene::Scene *scene)
    {
        if (!m_isInitialized || !targetCubemap || targetCubemap->GetType() != GL_TEXTURE_CUBE_MAP || resolution <= 0)
        {
            return false;
        }

        RenderTarget captureTarget(RenderTargetConfig{
            .width = resolution,
            .height = resolution,
            .clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        });

        const glm::vec3 directions[6] = {
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(-1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f, 0.0f, -1.0f),
        };
        const glm::vec3 upVectors[6] = {
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f, 0.0f, -1.0f),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f, -1.0f, 0.0f),
        };

        const auto previousDebugView = m_postProcessDebugView;
        m_postProcessDebugView = PostProcessDebugView::None;
        const float captureFarPlane = ResolveEnvironmentCaptureFarPlane(scene, position, farPlane);

        for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
        {
            CameraData cameraData;
            cameraData.view = glm::lookAt(position, position + directions[faceIndex], upVectors[faceIndex]);
            cameraData.projection = glm::perspective(glm::radians(90.0f), 1.0f, captureFarPlane, 0.1f);
            cameraData.nearPlane = 0.1f;
            cameraData.farPlane = captureFarPlane;

            RenderFrame(cameraData, &captureTarget, lights, nullptr, scene, false, false);

            Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, captureTarget.GetFramebufferID());
            Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, targetCubemap->GetTextureID());
            glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + faceIndex, 0, 0, 0, 0, 0, resolution, resolution);
        }

        Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, targetCubemap->GetTextureID());
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, 0);
        Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        m_postProcessDebugView = previousDebugView;
        if (auto frameResourceIt = m_frameResources.find(&captureTarget); frameResourceIt != m_frameResources.end())
        {
            if (auto &resources = frameResourceIt->second)
            {
                if (resources->temporaryRenderTarget)
                {
                    resources->temporaryRenderTarget->Cleanup();
                    resources->temporaryRenderTarget.reset();
                }
                if (resources->postProcessIntermediateRenderTarget)
                {
                    resources->postProcessIntermediateRenderTarget->Cleanup();
                    resources->postProcessIntermediateRenderTarget.reset();
                }
                resources->gBuffer.Cleanup();
            }
            m_frameResources.erase(frameResourceIt);
        }
        captureTarget.Cleanup();
        return true;
    }

    void Renderer::RenderFrame(const scene::CameraComponent &cameraComponent, RenderTarget *renderTarget, std::vector<scene::Light *> lights)
    {
        std::vector<IPostProcessEffect *> postProcessEffects;
        postProcessEffects.reserve(cameraComponent.GetPostProcessEffects().size());
        for (const auto &effect : cameraComponent.GetPostProcessEffects())
        {
            postProcessEffects.push_back(effect.get());
        }

        RenderFrame(cameraComponent.GetCameraData(renderTarget ? renderTarget->GetWidth() : (m_config.window ? m_config.window->GetExtents().width : 0),
                                                  renderTarget ? renderTarget->GetHeight() : (m_config.window ? m_config.window->GetExtents().height : 0)),
                    renderTarget,
                    std::move(lights),
                    &postProcessEffects,
                    cameraComponent.GetOwner() ? cameraComponent.GetOwner()->GetScene() : nullptr);
    }

    void Renderer::RenderFrame(const CameraData &cameraData, RenderTarget *renderTarget, std::vector<scene::Light *> lights, const std::vector<IPostProcessEffect *> *postProcessEffects, const scene::Scene *scene, bool renderEditorGrid, bool interactivePreview)
    {
        using ProfileClock = std::chrono::high_resolution_clock;
        const auto profileNow = [this]()
        {
            return m_config.enableProfiling ? ProfileClock::now() : ProfileClock::time_point{};
        };
        const auto renderFrameStart = profileNow();
        const auto elapsedMs = [](const auto &start, const auto &end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        };

        if (!m_isInitialized)
            return;

        if (!m_config.window || !m_config.window->EnsureOpenGLContextCurrent())
        {
            return;
        }

        if (postProcessEffects)
        {
            const bool hasUninitializedEffect = std::any_of(postProcessEffects->begin(), postProcessEffects->end(),
                                                            [](const IPostProcessEffect *effect)
                                                            {
                                                                return effect && effect->IsEnabled() && !effect->IsInitialized();
                                                            });
            if (hasUninitializedEffect && !m_config.window->EnsureOpenGLContextCurrent(true))
            {
                return;
            }

            for (auto *effect : *postProcessEffects)
            {
                if (effect && effect->IsEnabled())
                {
                    effect->EnsureInitialized();
                }
            }
        }

        Shader::ResetStateCache();
        Graphics::ResetStateCache();
        const auto contextSetupEnd = profileNow();
        if (m_config.enableProfiling)
            m_cpuFrameStats.renderFrameContextSetupMs += elapsedMs(renderFrameStart, contextSetupEnd);

        if (m_config.enableProfiling)
            ++m_profiledRenderCount;

        int renderWidth = 0;
        int renderHeight = 0;

        if (renderTarget)
        {
            renderWidth = renderTarget->GetWidth();
            renderHeight = renderTarget->GetHeight();
        }
        else if (m_config.window)
        {
            const auto extents = m_config.window->GetExtents();
            renderWidth = extents.width;
            renderHeight = extents.height;
        }

        if (renderWidth <= 0 || renderHeight <= 0)
        {
            return;
        }

        auto *frameResources = GetOrCreateFrameResources(renderTarget, renderWidth, renderHeight);
        if (!frameResources)
        {
            return;
        }

        CameraData activeCameraData = cameraData;
        if (auto *taaEffect = FindActiveTAAEffect(postProcessEffects))
        {
            activeCameraData = taaEffect->PrepareCameraData(cameraData, renderWidth, renderHeight, m_frameSequence);
        }
        frameResources->lastRenderedCameraData = activeCameraData;
        frameResources->hasLastRenderedCameraData = true;
        frameResources->lastUnjitteredCameraData = cameraData;
        frameResources->hasLastUnjitteredCameraData = true;
        const auto resourceSetupEnd = profileNow();
        if (m_config.enableProfiling)
            m_cpuFrameStats.renderFrameResourceSetupMs += elapsedMs(contextSetupEnd, resourceSetupEnd);

        const auto commandSortStart = resourceSetupEnd;
        EnsureRenderCommandsSorted();
        const auto commandSortEnd = profileNow();
        if (m_config.enableProfiling)
            m_cpuFrameStats.renderFrameCommandSortMs += elapsedMs(commandSortStart, commandSortEnd);

        const auto lodUpdateStart = commandSortEnd;
        UpdateRenderCommandLods(activeCameraData, renderHeight);
        const auto lodUpdateEnd = profileNow();
        if (m_config.enableProfiling)
            m_cpuFrameStats.renderFrameLodUpdateMs += elapsedMs(lodUpdateStart, lodUpdateEnd);

        const auto shadowPreparationStart = lodUpdateEnd;
        FinalizeShadowCommandSummary();
        const auto shadowPreparationEnd = profileNow();
        if (m_config.enableProfiling)
            m_cpuFrameStats.renderFrameShadowPreparationMs +=
                elapsedMs(shadowPreparationStart, shadowPreparationEnd);

        const auto visibilityStart = shadowPreparationEnd;
        m_visibleRenderCommands.clear();
        m_visibleRenderCommands.reserve(m_renderCommands.size());
        std::size_t visibleInstanceScratchCursor = 0;
        const auto frustumPlanes = ExtractFrustumPlanes(activeCameraData.projection * activeCameraData.view);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(activeCameraData.view)[3]);
        m_cpuFrameStats.visibleSingleLodCommandCount = 0;
        m_cpuFrameStats.visibleMultiLodCommandCount = 0;
        for (const auto &[commandIndex, fullyInsideFrustum] : m_visibilityCandidates)
        {
            if (commandIndex >= m_renderCommands.size())
                continue;
            const auto &command = m_renderCommands[commandIndex];
            if (visibleInstanceScratchCursor == m_visibleInstanceModelPool.size())
            {
                m_visibleInstanceModelPool.push_back(std::make_shared<std::vector<glm::mat4>>());
                m_visiblePreviousInstanceModelPool.push_back(std::make_shared<std::vector<glm::mat4>>());
            }
            const auto &modelScratch = m_visibleInstanceModelPool[visibleInstanceScratchCursor];
            const auto &previousModelScratch = m_visiblePreviousInstanceModelPool[visibleInstanceScratchCursor++];
            RenderCommand visibleCommand = CullRenderCommandInstances(
                command, cameraPosition, command.maxDrawDistance, &frustumPlanes,
                fullyInsideFrustum, modelScratch, previousModelScratch);
            if (visibleCommand.instanceModels && visibleCommand.instanceModels->empty())
                continue;
            m_visibleRenderCommands.push_back(std::move(visibleCommand));
            if (m_config.enableProfiling)
            {
                if (command.mesh && command.mesh->GetSubmeshLodCount(command.submeshIndex) > 1)
                    ++m_cpuFrameStats.visibleMultiLodCommandCount;
                else
                    ++m_cpuFrameStats.visibleSingleLodCommandCount;
            }
        }
        for (auto &detailTiming : m_gpuDetailTimings)
            detailTiming.hasResult = false;
        if (m_renderCommandsDirty)
            std::sort(m_visibleRenderCommands.begin(), m_visibleRenderCommands.end(), CompareRenderCommandKeysImpl);
        const auto visibilityEnd = profileNow();
        if (m_config.enableProfiling)
        {
            m_cpuFrameStats.visibleRenderCommandCount = static_cast<int>(m_visibleRenderCommands.size());
            m_cpuFrameStats.frustumCulledRenderCommandCount = static_cast<int>(m_renderCommands.size() - m_visibleRenderCommands.size());
            m_cpuFrameStats.renderFrameVisibilityMs += elapsedMs(visibilityStart, visibilityEnd);
        }

        RenderContext ctx{
            .renderer = this,
            .cameraData = activeCameraData,
            .unjitteredCameraData = cameraData,
            .previousCameraData = frameResources->previousCameraData,
            .hasCameraData = true,
            .hasPreviousCameraData = frameResources->hasPreviousCameraData,
            .cameraComponent = nullptr,
            .scene = scene,
            .postProcessEffects = postProcessEffects,
            .renderTarget = renderTarget,
            .temporaryRenderTarget = frameResources->temporaryRenderTarget.get(),
            .postProcessIntermediateRenderTarget = frameResources->postProcessIntermediateRenderTarget.get(),
            .renderCommands = &m_visibleRenderCommands,
            .decalCommands = &m_decalCommands,
            .lights = &lights,
            .gBuffer = &frameResources->gBuffer,
            .lightPropagationVolumePass = m_lightPropagationVolumePass,
            .postProcessDebugView = m_postProcessDebugView,
            .frameSequence = m_frameSequence,
            .renderEditorGrid = renderEditorGrid,
            .interactivePreview = interactivePreview,
            .oceanSurfaceDepthRenderTarget = frameResources->oceanSurfaceDepthRenderTarget.get(),
            .oceanSceneColorCopyRenderTarget = frameResources->oceanSceneColorCopyRenderTarget.get(),
            .shadowCasterCommandIndices = &m_shadowCasterCommandIndices,
            .shadowCasterFingerprint = m_shadowCasterFingerprint,
            .shadowCastersMoved = m_shadowCastersMoved,
            .allShadowCastersStatic = m_allShadowCastersStatic,
        };

        if (m_shadowPass)
        {
            const auto shadowSubmissionStart = profileNow();
            auto *shadowPass = static_cast<ShadowPass *>(m_shadowPass);
            RenderContext shadowCtx = ctx;
            shadowCtx.cameraData = cameraData;
            shadowCtx.previousCameraData = frameResources->previousShadowCameraData;
            shadowCtx.hasPreviousCameraData = frameResources->hasPreviousShadowCameraData;
            shadowCtx.renderCommands = &m_renderCommands;

            if (shadowPass->CanSkipStaticFrame(shadowCtx))
            {
                ExecutePassWithGpuTiming(*m_shadowPass, shadowCtx, 0);
            }
            else
            {
                // Keep the original command stream intact until the shadow pass
                // has established that a surface really needs updating. The
                // former eager copy transformed and distance-tested every
                // foliage instance even on frames that rendered no shadows.
                // Per-instance shadow-distance filtering now happens lazily in
                // ShadowPass while preparing an actual draw surface.
                ExecutePassWithGpuTiming(*m_shadowPass, shadowCtx, 0);
            }
            const auto shadowSubmissionEnd = profileNow();
            if (m_config.enableProfiling)
                m_cpuFrameStats.renderFrameShadowSubmissionMs += elapsedMs(shadowSubmissionStart, shadowSubmissionEnd);
        }

        const auto passSubmissionStart = profileNow();
        for (std::size_t index = 0; index < m_renderPasses.size(); ++index)
        {
            if (std::string_view(m_renderPasses[index]->GetName()) == "Volumetric Clouds")
            {
                ApplyAmbientOcclusionBeforeVolumetrics(ctx);
            }

            ExecutePassWithGpuTiming(*m_renderPasses[index], ctx, index + 1);
        }
        const auto passSubmissionEnd = profileNow();
        if (m_config.enableProfiling)
            m_cpuFrameStats.renderFramePassSubmissionMs += elapsedMs(passSubmissionStart, passSubmissionEnd);

        const auto finalizationStart = passSubmissionEnd;
        frameResources->previousCameraData = activeCameraData;
        frameResources->hasPreviousCameraData = true;
        frameResources->previousShadowCameraData = cameraData;
        frameResources->hasPreviousShadowCameraData = true;
        const auto renderFrameEnd = profileNow();
        if (m_config.enableProfiling)
        {
            m_cpuFrameStats.renderFrameFinalizationMs += elapsedMs(finalizationStart, renderFrameEnd);
            m_cpuFrameStats.renderFrameTotalMs += elapsedMs(renderFrameStart, renderFrameEnd);
        }
    }

    bool Renderer::GetLastRenderedCameraData(RenderTarget *renderTarget, CameraData &cameraData) const
    {
        const auto iterator = m_frameResources.find(renderTarget);
        if (iterator == m_frameResources.end() || !iterator->second || !iterator->second->hasLastRenderedCameraData)
        {
            return false;
        }

        cameraData = iterator->second->lastRenderedCameraData;
        return true;
    }

    bool Renderer::GetLastUnjitteredCameraData(RenderTarget *renderTarget, CameraData &cameraData) const
    {
        const auto iterator = m_frameResources.find(renderTarget);
        if (iterator == m_frameResources.end() || !iterator->second || !iterator->second->hasLastUnjitteredCameraData)
        {
            return false;
        }

        cameraData = iterator->second->lastUnjitteredCameraData;
        return true;
    }

    void Renderer::TrackShadowCommand(std::size_t commandIndex, const RenderCommand &command)
    {
        const bool castsShadow = IsShadowCasterCommand(command);
        m_shadowCasterSubmissionFlags.push_back(castsShadow ? 1u : 0u);
        if (!castsShadow)
            return;

        m_shadowCasterCommandIndices.push_back(commandIndex);
        m_shadowCastersMoved = m_shadowCastersMoved || command.skinningPoseChanged ||
                               !ShadowTransformsEqual(command.model, command.previousModel);
        m_allShadowCastersStatic = m_allShadowCastersStatic &&
                                   command.isStatic && command.jointMatrices == nullptr;
        HashShadowCommandValue(m_shadowCasterBaseFingerprint,
                               reinterpret_cast<std::uintptr_t>(command.mesh));
        HashShadowCommandValue(m_shadowCasterBaseFingerprint,
                               reinterpret_cast<std::uintptr_t>(command.material));
        HashShadowCommandValue(m_shadowCasterBaseFingerprint, command.submeshIndex);
    }

    void Renderer::FinalizeShadowCommandSummary()
    {
        m_shadowCasterFingerprint = m_shadowCasterBaseFingerprint;
        for (const std::size_t commandIndex : m_shadowCasterCommandIndices)
        {
            if (commandIndex < m_renderCommands.size())
                HashShadowCommandValue(m_shadowCasterFingerprint,
                                       m_renderCommands[commandIndex].lodIndex);
        }
        HashShadowCommandValue(m_shadowCasterFingerprint,
                               static_cast<std::uint64_t>(m_shadowCasterCommandIndices.size()));
    }

    void Renderer::SubmitRenderCommand(const RenderCommand &command)
    {
        if (!IsRenderCommandAcceptedForSubmission(command))
        {
            if (m_config.enableProfiling)
                ++m_cpuFrameStats.submissionCulledRenderCommandCount;
            return;
        }

        if (!m_renderCommands.empty() && CompareRenderCommandKeys(command, m_renderCommands.back()))
            m_renderCommandsDirty = true;
        const std::size_t commandIndex = m_renderCommands.size();
        m_renderCommands.push_back(command);
        TrackShadowCommand(commandIndex, command);
        if (m_config.enableProfiling)
            ++m_cpuFrameStats.submittedRenderCommandCount;
    }

    void Renderer::SubmitSortedRenderCommands(const std::vector<RenderCommand> &commands,
                                               bool applySubmissionCulling)
    {
        if (commands.empty())
            return;

        m_renderCommands.reserve(m_renderCommands.size() + commands.size());
        m_shadowCasterSubmissionFlags.reserve(m_renderCommands.capacity());
        bool insertedAny = false;
        for (const auto &command : commands)
        {
            if (applySubmissionCulling && !IsRenderCommandAcceptedForSubmission(command))
            {
                if (m_config.enableProfiling)
                    ++m_cpuFrameStats.submissionCulledRenderCommandCount;
                continue;
            }

            if (!insertedAny && !m_renderCommands.empty() &&
                CompareRenderCommandKeys(command, m_renderCommands.back()))
            {
                m_renderCommandsDirty = true;
            }
            const std::size_t commandIndex = m_renderCommands.size();
            m_renderCommands.push_back(command);
            TrackShadowCommand(commandIndex, command);
            if (m_config.enableProfiling)
                ++m_cpuFrameStats.submittedRenderCommandCount;
            insertedAny = true;
        }
    }

    void Renderer::ClearRenderCommands()
    {
        m_renderCommands.clear();
        m_shadowCasterCommandIndices.clear();
        m_shadowCasterSubmissionFlags.clear();
        m_shadowCasterBaseFingerprint = 1469598103934665603ull;
        m_shadowCasterFingerprint = m_shadowCasterBaseFingerprint;
        m_shadowCastersMoved = false;
        m_allShadowCastersStatic = true;
        m_decalCommands.clear();
        m_renderCommandsDirty = false;
        ClearSubmissionCullingCameras();
    }

    void Renderer::SetSubmissionCullingCameras(const std::vector<CameraData> &cameraDatas)
    {
        m_submissionFrustums.clear();
        m_submissionFrustums.reserve(cameraDatas.size());
        for (const auto &cameraData : cameraDatas)
        {
            SubmissionFrustum frustum;
            const auto planes = ExtractFrustumPlanes(cameraData.projection * cameraData.view);
            for (std::size_t index = 0; index < planes.size(); ++index)
            {
                frustum.planes[index] = ToSubmissionPlane(planes[index]);
            }
            m_submissionFrustums.push_back(frustum);
        }
    }

    void Renderer::ClearSubmissionCullingCameras()
    {
        m_submissionFrustums.clear();
    }

    void Renderer::EnsureRenderCommandsSorted()
    {
        if (!m_renderCommandsDirty || m_renderCommands.size() < 2)
        {
            m_renderCommandsDirty = false;
            return;
        }

        std::vector<std::uint64_t> identities;
        identities.reserve(m_renderCommands.size());
        for (const auto &command : m_renderCommands)
        {
            identities.push_back(RenderCommandSortIdentity(command));
        }

        if (identities == m_cachedSubmissionSortIdentities &&
            m_cachedSubmissionSortPermutation.size() == m_renderCommands.size())
        {
            std::vector<RenderCommand> reordered;
            std::vector<std::uint8_t> reorderedShadowFlags;
            reordered.reserve(m_renderCommands.size());
            reorderedShadowFlags.reserve(m_shadowCasterSubmissionFlags.size());
            for (const std::size_t sourceIndex : m_cachedSubmissionSortPermutation)
            {
                reordered.push_back(std::move(m_renderCommands[sourceIndex]));
                reorderedShadowFlags.push_back(m_shadowCasterSubmissionFlags[sourceIndex]);
            }
            m_renderCommands.swap(reordered);
            m_shadowCasterSubmissionFlags.swap(reorderedShadowFlags);
        }
        else
        {
            std::vector<std::size_t> permutation(m_renderCommands.size());
            std::iota(permutation.begin(), permutation.end(), std::size_t{0});
            std::sort(permutation.begin(), permutation.end(), [&](std::size_t a, std::size_t b)
            {
                return CompareRenderCommandKeysImpl(m_renderCommands[a], m_renderCommands[b]);
            });

            std::vector<RenderCommand> reordered;
            std::vector<std::uint8_t> reorderedShadowFlags;
            reordered.reserve(m_renderCommands.size());
            reorderedShadowFlags.reserve(m_shadowCasterSubmissionFlags.size());
            for (const std::size_t sourceIndex : permutation)
            {
                reordered.push_back(std::move(m_renderCommands[sourceIndex]));
                reorderedShadowFlags.push_back(m_shadowCasterSubmissionFlags[sourceIndex]);
            }
            m_renderCommands.swap(reordered);
            m_shadowCasterSubmissionFlags.swap(reorderedShadowFlags);
            m_cachedSubmissionSortIdentities = std::move(identities);
            m_cachedSubmissionSortPermutation = std::move(permutation);
            ++m_cpuFrameStats.renderCommandSortCount;
        }
        m_shadowCasterCommandIndices.clear();
        m_shadowCasterCommandIndices.reserve(m_shadowCasterSubmissionFlags.size());
        for (std::size_t commandIndex = 0;
             commandIndex < m_shadowCasterSubmissionFlags.size(); ++commandIndex)
        {
            if (m_shadowCasterSubmissionFlags[commandIndex] != 0)
                m_shadowCasterCommandIndices.push_back(commandIndex);
        }
        m_renderCommandsDirty = false;
    }

    void Renderer::UpdateRenderCommandLods(const CameraData &cameraData, int viewportHeight)
    {
        constexpr float kLodTransitionWidth = 0.15f;
        const glm::mat4 inverseView = glm::inverse(cameraData.view);
        const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
        const float projectionScaleY = std::abs(cameraData.projection[1][1]);
        const float halfViewportHeight = static_cast<float>(std::max(viewportHeight, 1)) * 0.5f;
        const auto frustumPlanes = ExtractFrustumPlanes(cameraData.projection * cameraData.view);
        bool changed = false;
        m_visibilityCandidates.clear();
        m_visibilityCandidates.reserve(m_renderCommands.size());

        for (std::size_t commandIndex = 0; commandIndex < m_renderCommands.size(); ++commandIndex)
        {
            auto &command = m_renderCommands[commandIndex];
            if (!command.mesh)
            {
                continue;
            }

            // A generated fallback UV2 atlas is built against the source
            // submesh triangles. Generated geometry LODs have different
            // topology, so they cannot safely sample that same lightmap.
            if (command.mesh->HasGeneratedLightmapUvsForSubmesh(command.submeshIndex))
            {
                if (command.lodIndex != 0 ||
                    command.GetLodTransitionIndex() != 0 ||
                    command.GetLodTransitionFade() != 0.0f)
                {
                    command.lodIndex = 0;
                    command.SetLodTransition(0, 0.0f);
                    changed = true;
                }
            }
            else if (command.mesh->GetSubmeshLodCount(command.submeshIndex) > 1)
            {
                const glm::vec3 cameraOffset = command.worldBounds.center - cameraPosition;
                const float safeDistance = std::sqrt(std::max(glm::dot(cameraOffset, cameraOffset), 0.000001f));
                const float projectedRadiusPixels = (std::max(command.worldBounds.radius, 0.001f) / safeDistance) * projectionScaleY * halfViewportHeight;
                const uint32_t selectedLodIndex = static_cast<uint32_t>(command.mesh->SelectSubmeshLodByProjectedRadius(command.submeshIndex, projectedRadiusPixels));
                const std::size_t lodCount = command.mesh->GetSubmeshLodCount(command.submeshIndex);
                const uint32_t minLodIndex = lodCount > 0 ? std::min(command.GetMinLodIndex(), static_cast<uint32_t>(lodCount - 1)) : 0u;
                const uint32_t lodIndex = std::max(selectedLodIndex, minLodIndex);
                uint32_t transitionIndex = lodIndex;
                uint32_t transitionBaseIndex = lodIndex;
                float transitionFade = 0.0f;

                for (uint32_t farLodIndex = 1; farLodIndex < lodCount; ++farLodIndex)
                {
                    const uint32_t nearLodIndex = farLodIndex - 1;
                    if (nearLodIndex < minLodIndex)
                        continue;
                    const float threshold = command.mesh->GetSubmeshLodRange(command.submeshIndex, farLodIndex).maxScreenRadiusPixels;
                    if (!std::isfinite(threshold) || threshold <= 0.0f)
                        continue;
                    const float upperRadius = threshold * (1.0f + kLodTransitionWidth);
                    const float lowerRadius = threshold * (1.0f - kLodTransitionWidth);
                    if (projectedRadiusPixels <= upperRadius && projectedRadiusPixels >= lowerRadius)
                    {
                        transitionBaseIndex = nearLodIndex;
                        transitionIndex = farLodIndex;
                        transitionFade = glm::clamp((upperRadius - projectedRadiusPixels) /
                                                        std::max(upperRadius - lowerRadius, 0.001f),
                                                    0.0f, 1.0f);
                        break;
                    }
                }

                const uint32_t resolvedLodIndex = transitionFade > 0.0f && transitionFade < 1.0f
                                                      ? transitionBaseIndex
                                                      : lodIndex;
                if (command.lodIndex != resolvedLodIndex ||
                    command.GetLodTransitionIndex() != transitionIndex ||
                    std::abs(command.GetLodTransitionFade() - transitionFade) > (1.0f / 65535.0f))
                {
                    command.lodIndex = resolvedLodIndex;
                    command.SetLodTransition(transitionIndex, transitionFade);
                    changed = true;
                }
            }

            const FrustumContainment containment = ClassifyBounds(command.worldBounds, frustumPlanes);
            if (containment != FrustumContainment::Outside &&
                PassesDistanceCull(command, cameraPosition, command.maxDrawDistance) &&
                PassesStaticProjectedSizeCull(command, cameraPosition, projectionScaleY, halfViewportHeight))
            {
                m_visibilityCandidates.emplace_back(commandIndex, containment == FrustumContainment::Inside);
            }
        }

        if (changed)
        {
            m_renderCommandsDirty = true;
        }
    }

    bool Renderer::IsRenderCommandAcceptedForSubmission(const RenderCommand &command) const
    {
        if (m_submissionFrustums.empty())
        {
            return true;
        }

        for (const auto &frustum : m_submissionFrustums)
        {
            bool visible = true;
            for (const auto &plane : frustum.planes)
            {
                if (glm::dot(glm::vec3(plane), command.worldBounds.center) + plane.w < -command.worldBounds.radius)
                {
                    visible = false;
                    break;
                }
            }

            if (visible)
            {
                return true;
            }
        }

        // Camera-frustum submission culling must not remove potential shadow
        // casters. An object outside every camera view can still lie inside a
        // light's shadow frustum and cast onto visible geometry. The shadow
        // pass performs its own light-space culling, while RenderFrame builds
        // a separately camera-culled list for the geometry passes.
        return command.castsShadow &&
               command.material &&
               command.material->GetConfig().castsShadow &&
               command.material->GetConfig().alphaMode != AlphaMode::Blend;
    }

    bool Renderer::CompareRenderCommandKeys(const RenderCommand &a, const RenderCommand &b)
    {
        return CompareRenderCommandKeysImpl(a, b);
    }

    void Renderer::EndFrame(RenderTarget *renderTarget)
    {
        m_gpuTimingsResolvedThisFrame = false;
        if (renderTarget)
        {
            Graphics::UnbindRenderTarget();
            return;
        }

        if (m_config.window)
        {
            glfwSwapBuffers(static_cast<GLFWwindow *>(m_config.window->GetWindow()));
        }
    }

    void Renderer::Shutdown(RenderTarget *renderTarget)
    {
        // Clean up rendering resources here
        RmlUiRuntime::Get().Shutdown();
        m_isInitialized = false;
        CleanupResources(renderTarget);
        CleanupFrameResources();
        if (m_shadowPass)
        {
            delete m_shadowPass;
            m_shadowPass = nullptr;
        }
        m_lightPropagationVolumePass = nullptr;
        m_physicalSkyPass = nullptr;
        ShutdownGpuTimers();
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    bool Renderer::PreparePhysicalSkyEnvironment(const RenderContext &ctx)
    {
        return m_physicalSkyPass && m_physicalSkyPass->PrepareEnvironment(ctx);
    }

    GLuint Renderer::GetPhysicalSkyEnvironmentTextureID() const
    {
        return m_physicalSkyPass ? m_physicalSkyPass->GetEnvironmentTextureID() : 0;
    }

    int Renderer::GetPhysicalSkyEnvironmentWidth() const
    {
        return m_physicalSkyPass ? m_physicalSkyPass->GetEnvironmentWidth() : 0;
    }

    int Renderer::GetPhysicalSkyEnvironmentHeight() const
    {
        return m_physicalSkyPass ? m_physicalSkyPass->GetEnvironmentHeight() : 0;
    }

    float Renderer::GetPhysicalSkyDirectionalLightVisibility(const scene::Light *light) const
    {
        return m_physicalSkyPass ? m_physicalSkyPass->GetDirectionalLightVisibility(light) : 1.0f;
    }

    void Renderer::BeginLightingStageTiming(std::size_t stageIndex)
    {
        if (!m_gpuProfilingSupported || stageIndex >= m_lightingGpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_lightingGpuTimerQueries[stageIndex];
        if (!m_gpuTimingsResolvedThisFrame)
        {
            ResolveAllLightingGpuTimings(stageIndex);
        }

        auto queryIndex = queryState.writeIndex;
        if (queryState.pending[queryIndex])
        {
            const auto freeQueryIt = std::find(queryState.pending.begin(), queryState.pending.end(), false);
            if (freeQueryIt == queryState.pending.end())
            {
                return;
            }
            queryIndex = static_cast<std::size_t>(std::distance(queryState.pending.begin(), freeQueryIt));
            queryState.writeIndex = queryIndex;
        }

        switch (stageIndex)
        {
        case kLightingSetupStage:
            break;
        case kLightingAmbientStage:
            break;
        case kLightingAccumulationStage:
            break;
        default:
            return;
        }

        glBeginQuery(GL_TIME_ELAPSED, queryState.queryIds[queryIndex]);
        queryState.activeIndex = queryIndex;
        queryState.active = true;
    }

    void Renderer::EndLightingStageTiming(std::size_t stageIndex)
    {
        if (!m_gpuProfilingSupported || stageIndex >= m_lightingGpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_lightingGpuTimerQueries[stageIndex];
        if (!queryState.active)
        {
            return;
        }

        glEndQuery(GL_TIME_ELAPSED);
        queryState.pending[queryState.activeIndex] = true;
        queryState.writeIndex = (queryState.activeIndex + 1) % queryState.queryIds.size();
        queryState.active = false;
    }

    void Renderer::SetLightingPassCounters(int lightCount, int shadowedLightCount)
    {
        m_lightingGpuTiming.lightCount = lightCount;
        m_lightingGpuTiming.shadowedLightCount = shadowedLightCount;
    }

    bool Renderer::BeginPostProcessEffectTiming(std::string_view effectName)
    {
        if (!m_gpuProfilingSupported || m_postProcessGpuTimingActive)
        {
            return false;
        }

        const std::size_t timingIndex = EnsurePostProcessGpuTiming(effectName);
        m_postProcessGpuTimings[timingIndex].hasResult =
            m_postProcessGpuTimingHasCachedResult[timingIndex];
        UpdatePostProcessGpuTimingTotal();
        if (!m_gpuTimingsResolvedThisFrame)
        {
            ResolveAllPostProcessGpuTimings(timingIndex);
        }

        auto &queryState = m_postProcessGpuTimerQueries[timingIndex];
        auto queryIndex = queryState.writeIndex;
        if (queryState.pending[queryIndex])
        {
            const auto freeQueryIt = std::find(queryState.pending.begin(), queryState.pending.end(), false);
            if (freeQueryIt == queryState.pending.end())
            {
                return false;
            }
            queryIndex = static_cast<std::size_t>(std::distance(queryState.pending.begin(), freeQueryIt));
            queryState.writeIndex = queryIndex;
        }

        glBeginQuery(GL_TIME_ELAPSED, queryState.queryIds[queryIndex]);
        queryState.activeIndex = queryIndex;
        queryState.active = true;
        m_activePostProcessGpuTimingIndex = timingIndex;
        m_postProcessGpuTimingActive = true;
        return true;
    }

    void Renderer::EndPostProcessEffectTiming()
    {
        if (!m_gpuProfilingSupported || !m_postProcessGpuTimingActive || m_activePostProcessGpuTimingIndex >= m_postProcessGpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_postProcessGpuTimerQueries[m_activePostProcessGpuTimingIndex];
        if (!queryState.active)
        {
            m_postProcessGpuTimingActive = false;
            return;
        }

        glEndQuery(GL_TIME_ELAPSED);
        queryState.pending[queryState.activeIndex] = true;
        queryState.writeIndex = (queryState.activeIndex + 1) % queryState.queryIds.size();
        queryState.active = false;
        m_postProcessGpuTimingActive = false;
    }

    bool Renderer::BeginGpuDetailTiming(std::string_view name)
    {
        if (!m_gpuProfilingSupported || m_gpuDetailTimingActive || name.empty())
            return false;

        const std::size_t timingIndex = EnsureGpuDetailTiming(name);
        auto &queryState = m_gpuDetailTimerQueries[timingIndex];
        std::size_t queryIndex = queryState.writeIndex;
        if (queryState.pending[queryIndex])
        {
            const auto freeQuery = std::find(queryState.pending.begin(), queryState.pending.end(), false);
            if (freeQuery == queryState.pending.end())
                return false;
            queryIndex = static_cast<std::size_t>(std::distance(queryState.pending.begin(), freeQuery));
        }

        glQueryCounter(queryState.startQueryIds[queryIndex], GL_TIMESTAMP);
        queryState.activeIndex = queryIndex;
        queryState.active = true;
        m_activeGpuDetailTimingIndex = timingIndex;
        m_gpuDetailTimingActive = true;
        return true;
    }

    void Renderer::EndGpuDetailTiming()
    {
        if (!m_gpuProfilingSupported || !m_gpuDetailTimingActive ||
            m_activeGpuDetailTimingIndex >= m_gpuDetailTimerQueries.size())
            return;

        auto &queryState = m_gpuDetailTimerQueries[m_activeGpuDetailTimingIndex];
        if (queryState.active)
        {
            glQueryCounter(queryState.endQueryIds[queryState.activeIndex], GL_TIMESTAMP);
            queryState.pending[queryState.activeIndex] = true;
            queryState.writeIndex = (queryState.activeIndex + 1) % queryState.pending.size();
            queryState.active = false;
        }
        m_gpuDetailTimingActive = false;
    }

    std::size_t Renderer::EnsureGpuDetailTiming(std::string_view name)
    {
        const std::string timingName(name);
        if (const auto found = m_gpuDetailTimingIndices.find(timingName);
            found != m_gpuDetailTimingIndices.end())
            return found->second;

        const std::size_t index = m_gpuDetailTimings.size();
        m_gpuDetailTimingIndices.emplace(timingName, index);
        m_gpuDetailTimings.push_back(GpuPassTiming{timingName});
        auto &queries = m_gpuDetailTimerQueries.emplace_back();
        glGenQueries(static_cast<GLsizei>(queries.startQueryIds.size()), queries.startQueryIds.data());
        glGenQueries(static_cast<GLsizei>(queries.endQueryIds.size()), queries.endQueryIds.data());
        return index;
    }

    void Renderer::ResolveAllGpuDetailTimings()
    {
        if (!m_gpuProfilingSupported)
            return;

        for (std::size_t timingIndex = 0; timingIndex < m_gpuDetailTimerQueries.size(); ++timingIndex)
        {
            auto &queries = m_gpuDetailTimerQueries[timingIndex];
            for (std::size_t queryIndex = 0; queryIndex < queries.pending.size(); ++queryIndex)
            {
                if (!queries.pending[queryIndex])
                    continue;
                GLuint available = GL_FALSE;
                glGetQueryObjectuiv(queries.endQueryIds[queryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
                if (available != GL_TRUE)
                    continue;
                GLuint64 start = 0;
                GLuint64 end = 0;
                glGetQueryObjectui64v(queries.startQueryIds[queryIndex], GL_QUERY_RESULT, &start);
                glGetQueryObjectui64v(queries.endQueryIds[queryIndex], GL_QUERY_RESULT, &end);
                m_gpuDetailTimings[timingIndex].gpuTimeMs =
                    static_cast<float>(end >= start ? end - start : 0) * kNanosecondsToMilliseconds;
                m_gpuDetailTimings[timingIndex].hasResult = true;
                queries.pending[queryIndex] = false;
            }
        }
    }

    void Renderer::RecordGBufferResize(float resizeMs)
    {
        m_cpuFrameStats.gBufferResizeMs += resizeMs;
        ++m_cpuFrameStats.gBufferResizeCount;
    }

    void Renderer::RecordShadowMapUpdate(int surfacePixels, int submittedInstances, int submittedBatches, int submittedTriangles, int materialGroups, int apiDrawCalls, bool directionalCascade)
    {
        ++m_cpuFrameStats.shadowUpdatedSurfaceCount;
        if (directionalCascade)
        {
            ++m_cpuFrameStats.shadowUpdatedDirectionalCascadeCount;
        }
        m_cpuFrameStats.shadowUpdatedPixelCount += std::max(surfacePixels, 0);
        m_cpuFrameStats.shadowSubmittedInstanceCount += std::max(submittedInstances, 0);
        m_cpuFrameStats.shadowSubmittedBatchCount += std::max(submittedBatches, 0);
        m_cpuFrameStats.shadowMaterialGroupCount += std::max(materialGroups, 0);
        m_cpuFrameStats.shadowApiDrawCallCount += std::max(apiDrawCalls, 0);
        m_cpuFrameStats.shadowSubmittedTriangleCount += std::max(submittedTriangles, 0);
    }

    void Renderer::PrepareVisibleRenderCommands(const CameraData &cameraData, int viewportHeight)
    {
        EnsureRenderCommandsSorted();
        UpdateRenderCommandLods(cameraData, viewportHeight);
        m_visibleRenderCommands.clear();
        m_visibleRenderCommands.reserve(m_renderCommands.size());
        std::size_t visibleInstanceScratchCursor = 0;
        const auto frustumPlanes = ExtractFrustumPlanes(cameraData.projection * cameraData.view);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        for (const auto &[commandIndex, fullyInsideFrustum] : m_visibilityCandidates)
        {
            if (commandIndex >= m_renderCommands.size())
                continue;
            const auto &command = m_renderCommands[commandIndex];
            if (visibleInstanceScratchCursor == m_visibleInstanceModelPool.size())
            {
                m_visibleInstanceModelPool.push_back(std::make_shared<std::vector<glm::mat4>>());
                m_visiblePreviousInstanceModelPool.push_back(std::make_shared<std::vector<glm::mat4>>());
            }
            const auto &modelScratch = m_visibleInstanceModelPool[visibleInstanceScratchCursor];
            const auto &previousModelScratch = m_visiblePreviousInstanceModelPool[visibleInstanceScratchCursor++];
            RenderCommand visibleCommand = CullRenderCommandInstances(
                command, cameraPosition, command.maxDrawDistance, &frustumPlanes,
                fullyInsideFrustum, modelScratch, previousModelScratch);
            if (visibleCommand.instanceModels && visibleCommand.instanceModels->empty())
                continue;
            m_visibleRenderCommands.push_back(std::move(visibleCommand));
        }
        if (m_renderCommandsDirty)
            std::sort(m_visibleRenderCommands.begin(), m_visibleRenderCommands.end(), CompareRenderCommandKeysImpl);
    }

    void Renderer::RecordShadowCpuBreakdown(float targetBindMs, float casterBatchBuildMs,
                                             float bufferUploadMs, float drawSubmissionMs,
                                             float imageCopyMs, float measuredPassMs,
                                             int batchBuildCount, int imageCopyCount)
    {
        m_cpuFrameStats.shadowCpuTargetBindMs += std::max(targetBindMs, 0.0f);
        m_cpuFrameStats.shadowCpuCasterBatchBuildMs += std::max(casterBatchBuildMs, 0.0f);
        m_cpuFrameStats.shadowCpuBufferUploadMs += std::max(bufferUploadMs, 0.0f);
        m_cpuFrameStats.shadowCpuDrawSubmissionMs += std::max(drawSubmissionMs, 0.0f);
        m_cpuFrameStats.shadowCpuImageCopyMs += std::max(imageCopyMs, 0.0f);
        const float classifiedMs = targetBindMs + casterBatchBuildMs + bufferUploadMs +
                                   drawSubmissionMs + imageCopyMs;
        m_cpuFrameStats.shadowCpuUnclassifiedMs += std::max(measuredPassMs - classifiedMs, 0.0f);
        m_cpuFrameStats.shadowCpuBatchBuildCount += std::max(batchBuildCount, 0);
        m_cpuFrameStats.shadowCpuImageCopyCount += std::max(imageCopyCount, 0);
    }

    void Renderer::RecordDirectionalShadowScroll(bool candidate, bool succeeded, bool topologyRejected,
                                                  float maxMatrixDelta, float fractionalTexelError)
    {
        if (candidate) ++m_cpuFrameStats.shadowCascadeScrollCandidateCount;
        if (succeeded) ++m_cpuFrameStats.shadowCascadeScrollSuccessCount;
        if (topologyRejected) ++m_cpuFrameStats.shadowCascadeScrollTopologyRejectedCount;
        m_cpuFrameStats.shadowCascadeScrollMaxMatrixDelta =
            std::max(m_cpuFrameStats.shadowCascadeScrollMaxMatrixDelta, std::max(maxMatrixDelta, 0.0f));
        m_cpuFrameStats.shadowCascadeScrollMaxFractionalTexelError =
            std::max(m_cpuFrameStats.shadowCascadeScrollMaxFractionalTexelError, std::max(fractionalTexelError, 0.0f));
    }

    void Renderer::RecordGeometryBatch(int submittedInstances, int submittedTriangles, std::size_t lodIndex)
    {
        m_cpuFrameStats.geometrySubmittedInstanceCount += std::max(submittedInstances, 0);
        m_cpuFrameStats.geometrySubmittedTriangleCount += std::max(submittedTriangles, 0);
        const std::size_t clampedLodIndex = std::min(lodIndex, m_cpuFrameStats.geometrySubmittedTrianglesByLod.size() - 1);
        m_cpuFrameStats.geometrySubmittedTrianglesByLod[clampedLodIndex] += std::max(submittedTriangles, 0);
        ++m_cpuFrameStats.geometrySubmittedBatchCount;
    }

    void Renderer::RecordGeometryDriverSubmission(int materialGroups, int apiDrawCalls)
    {
        m_cpuFrameStats.geometryMaterialGroupCount += std::max(materialGroups, 0);
        m_cpuFrameStats.geometryApiDrawCallCount += std::max(apiDrawCalls, 0);
    }

    float Renderer::GetTotalGpuPassTimeMs() const
    {
        return std::accumulate(
            m_gpuPassTimings.begin(),
            m_gpuPassTimings.end(),
            0.0f,
            [](float total, const GpuPassTiming &timing)
            {
                return total + (timing.hasResult ? timing.gpuTimeMs : 0.0f);
            });
    }

    float Renderer::GetTotalCpuPassTimeMs() const
    {
        return std::accumulate(
            m_cpuPassTimings.begin(),
            m_cpuPassTimings.end(),
            0.0f,
            [](float total, const CpuPassTiming &timing)
            {
                return total + timing.cpuTimeMs;
            });
    }

    void Renderer::SetVSyncEnabled(bool enabled)
    {
        m_vsyncEnabled = enabled;
        glfwSwapInterval(enabled ? 1 : 0);
    }

    void Renderer::CleanupResources(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            renderTarget->Cleanup();
            delete renderTarget;
            renderTarget = nullptr;
        }
        else
        {
            // Clean up any other resources if needed
        }
    }

    Renderer::FrameResources *Renderer::GetOrCreateFrameResources(RenderTarget *renderTarget, int width, int height)
    {
        auto &entry = m_frameResources[renderTarget];
        if (!entry)
        {
            entry = std::make_unique<FrameResources>();
        }

        auto ensureSizedRenderTarget = [this, width, height](std::unique_ptr<RenderTarget> &target)
        {
            if (!target)
            {
                const auto resizeStart = std::chrono::high_resolution_clock::now();
                target = std::make_unique<RenderTarget>(RenderTargetConfig{width, height, glm::vec4(0.0f)});
                const auto resizeEnd = std::chrono::high_resolution_clock::now();
                m_cpuFrameStats.intermediateTargetResizeMs += std::chrono::duration<float, std::milli>(resizeEnd - resizeStart).count();
                ++m_cpuFrameStats.intermediateTargetResizeCount;
                return target->IsInitialized();
            }

            if (target->GetWidth() == width && target->GetHeight() == height && target->IsInitialized())
            {
                return true;
            }

            const auto resizeStart = std::chrono::high_resolution_clock::now();
            const bool resized = EnsureRenderTargetSize(target.get(), width, height);
            const auto resizeEnd = std::chrono::high_resolution_clock::now();
            m_cpuFrameStats.intermediateTargetResizeMs += std::chrono::duration<float, std::milli>(resizeEnd - resizeStart).count();
            ++m_cpuFrameStats.intermediateTargetResizeCount;
            return resized;
        };

        if (!ensureSizedRenderTarget(entry->temporaryRenderTarget) ||
            !ensureSizedRenderTarget(entry->postProcessIntermediateRenderTarget) ||
            !ensureSizedRenderTarget(entry->oceanSurfaceDepthRenderTarget) ||
            !ensureSizedRenderTarget(entry->oceanSceneColorCopyRenderTarget))
        {
            std::cerr << "Failed to resize post process render targets" << std::endl;
            return nullptr;
        }

        return entry.get();
    }

    void Renderer::CleanupFrameResources()
    {
        for (auto &[key, resources] : m_frameResources)
        {
            if (!resources)
            {
                continue;
            }

            if (resources->temporaryRenderTarget)
            {
                resources->temporaryRenderTarget->Cleanup();
                resources->temporaryRenderTarget.reset();
            }

            if (resources->postProcessIntermediateRenderTarget)
            {
                resources->postProcessIntermediateRenderTarget->Cleanup();
                resources->postProcessIntermediateRenderTarget.reset();
            }

            if (resources->oceanSurfaceDepthRenderTarget)
            {
                resources->oceanSurfaceDepthRenderTarget->Cleanup();
                resources->oceanSurfaceDepthRenderTarget.reset();
            }

            if (resources->oceanSceneColorCopyRenderTarget)
            {
                resources->oceanSceneColorCopyRenderTarget->Cleanup();
                resources->oceanSceneColorCopyRenderTarget.reset();
            }

            resources->gBuffer.Cleanup();
        }

        m_frameResources.clear();
    }

    void Renderer::InitializeGpuTimers()
    {
        m_cpuPassTimings.clear();
        m_gpuPassTimings.clear();
        m_gpuTimerQueries.clear();
        m_postProcessGpuTimerQueries.clear();
        m_postProcessGpuTimings.clear();
        m_postProcessGpuTimingHasCachedResult.clear();
        m_postProcessGpuTimingIndices.clear();
        m_gpuDetailTimings.clear();
        m_gpuDetailTimerQueries.clear();
        m_gpuDetailTimingIndices.clear();
        m_gpuProfilingSupported = m_config.enableProfiling && GLAD_GL_VERSION_3_3;
        if (!m_gpuProfilingSupported)
        {
            return;
        }

        if (m_shadowPass)
        {
            m_cpuPassTimings.push_back(CpuPassTiming{m_shadowPass->GetName()});
            m_gpuPassTimings.push_back(GpuPassTiming{m_shadowPass->GetName()});
        }

        for (auto *pass : m_renderPasses)
        {
            m_cpuPassTimings.push_back(CpuPassTiming{pass->GetName()});
            m_gpuPassTimings.push_back(GpuPassTiming{pass->GetName()});
        }

        m_gpuTimerQueries.resize(m_gpuPassTimings.size());
        for (auto &queryState : m_gpuTimerQueries)
        {
            glGenQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
        }

        for (auto &queryState : m_lightingGpuTimerQueries)
        {
            glGenQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
        }

        m_lightingGpuTiming = {};
        m_cpuFrameStats = {};
        m_postProcessGpuTimingActive = false;
        m_gpuDetailTimingActive = false;
    }

    void Renderer::ShutdownGpuTimers()
    {
        if (m_gpuProfilingSupported)
        {
            for (auto &queryState : m_gpuTimerQueries)
            {
                glDeleteQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
                queryState.queryIds = {};
                queryState.pending = {};
                queryState.writeIndex = 0;
                queryState.activeIndex = 0;
                queryState.active = false;
            }

            for (auto &queryState : m_lightingGpuTimerQueries)
            {
                glDeleteQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
                queryState.queryIds = {};
                queryState.pending = {};
                queryState.writeIndex = 0;
                queryState.activeIndex = 0;
                queryState.active = false;
            }

            for (auto &queryState : m_postProcessGpuTimerQueries)
            {
                glDeleteQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
                queryState.queryIds = {};
                queryState.pending = {};
                queryState.writeIndex = 0;
                queryState.activeIndex = 0;
                queryState.active = false;
            }
            for (auto &queryState : m_gpuDetailTimerQueries)
            {
                glDeleteQueries(static_cast<GLsizei>(queryState.startQueryIds.size()), queryState.startQueryIds.data());
                glDeleteQueries(static_cast<GLsizei>(queryState.endQueryIds.size()), queryState.endQueryIds.data());
            }
        }

        m_gpuTimerQueries.clear();
        m_cpuPassTimings.clear();
        m_gpuPassTimings.clear();
        m_postProcessGpuTimerQueries.clear();
        m_postProcessGpuTimings.clear();
        m_postProcessGpuTimingHasCachedResult.clear();
        m_postProcessGpuTimingIndices.clear();
        m_gpuDetailTimings.clear();
        m_gpuDetailTimerQueries.clear();
        m_gpuDetailTimingIndices.clear();
        m_lightingGpuTiming = {};
        m_cpuFrameStats = {};
        m_gpuProfilingSupported = false;
        m_postProcessGpuTimingActive = false;
        m_gpuDetailTimingActive = false;
    }

    void Renderer::ExecutePassWithGpuTiming(IRenderPass &renderPass, const RenderContext &ctx, std::size_t timingIndex)
    {
        if (!m_config.enableProfiling)
        {
            renderPass.Execute(ctx);
            return;
        }

        const auto cpuPassStart = std::chrono::high_resolution_clock::now();
        const bool isLightingPass = std::string_view(renderPass.GetName()) == "Lighting";
        const bool isPostProcessPass = std::string_view(renderPass.GetName()) == "Post Process";

        if (isLightingPass)
        {
            renderPass.Execute(ctx);
            const auto cpuPassEnd = std::chrono::high_resolution_clock::now();

            const float lightingGpuTimeAfter = m_lightingGpuTiming.setupMs +
                                               m_lightingGpuTiming.ambientMs +
                                               m_lightingGpuTiming.lightAccumulationMs;

            if (timingIndex < m_gpuPassTimings.size())
            {
                auto &lightingPassTiming = m_gpuPassTimings[timingIndex];
                if (m_lightingGpuTiming.hasSetupResult &&
                    m_lightingGpuTiming.hasAmbientResult &&
                    m_lightingGpuTiming.hasLightAccumulationResult)
                {
                    lightingPassTiming.gpuTimeMs = lightingGpuTimeAfter;
                    lightingPassTiming.hasResult = true;
                }
            }

            if (timingIndex < m_cpuPassTimings.size())
            {
                m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
            }
            return;
        }

        if (isPostProcessPass)
        {
            renderPass.Execute(ctx);
            const auto cpuPassEnd = std::chrono::high_resolution_clock::now();
            if (timingIndex < m_cpuPassTimings.size())
            {
                m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
            }
            return;
        }

        if (!m_gpuProfilingSupported || timingIndex >= m_gpuTimerQueries.size())
        {
            renderPass.Execute(ctx);
            const auto cpuPassEnd = std::chrono::high_resolution_clock::now();
            if (timingIndex < m_cpuPassTimings.size())
            {
                m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
            }
            return;
        }

        auto &queryState = m_gpuTimerQueries[timingIndex];
        if (!m_gpuTimingsResolvedThisFrame)
        {
            ResolveAllGpuTimings(timingIndex);
        }

        auto queryIndex = queryState.writeIndex;
        if (queryState.pending[queryIndex])
        {
            const auto freeQueryIt = std::find(queryState.pending.begin(), queryState.pending.end(), false);
            if (freeQueryIt == queryState.pending.end())
            {
                renderPass.Execute(ctx);
                const auto cpuPassEnd = std::chrono::high_resolution_clock::now();
                if (timingIndex < m_cpuPassTimings.size())
                {
                    m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
                }
                return;
            }
            queryIndex = static_cast<std::size_t>(std::distance(queryState.pending.begin(), freeQueryIt));
            queryState.writeIndex = queryIndex;
        }

        glBeginQuery(GL_TIME_ELAPSED, queryState.queryIds[queryIndex]);
        renderPass.Execute(ctx);
        const auto cpuPassEnd = std::chrono::high_resolution_clock::now();
        glEndQuery(GL_TIME_ELAPSED);

        queryState.pending[queryIndex] = true;
        queryState.writeIndex = (queryState.writeIndex + 1) % queryState.queryIds.size();

        if (timingIndex < m_cpuPassTimings.size())
        {
            m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
        }
    }

    void Renderer::ResolveAllGpuTimings()
    {
        if (!m_gpuProfilingSupported)
        {
            return;
        }

        for (std::size_t timingIndex = 0; timingIndex < m_gpuTimerQueries.size(); ++timingIndex)
        {
            ResolveAllGpuTimings(timingIndex);
        }
    }

    void Renderer::ResolveAllGpuTimings(std::size_t timingIndex)
    {
        if (!m_gpuProfilingSupported || timingIndex >= m_gpuTimerQueries.size())
        {
            return;
        }

        for (std::size_t queryIndex = 0; queryIndex < m_gpuTimerQueries[timingIndex].queryIds.size(); ++queryIndex)
        {
            ResolveGpuTiming(timingIndex, queryIndex);
        }
    }

    void Renderer::ResolveAllLightingGpuTimings()
    {
        if (!m_gpuProfilingSupported)
        {
            return;
        }

        for (std::size_t stageIndex = 0; stageIndex < m_lightingGpuTimerQueries.size(); ++stageIndex)
        {
            ResolveAllLightingGpuTimings(stageIndex);
        }
    }

    void Renderer::ResolveAllLightingGpuTimings(std::size_t stageIndex)
    {
        if (!m_gpuProfilingSupported || stageIndex >= m_lightingGpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_lightingGpuTimerQueries[stageIndex];
        for (std::size_t queryIndex = 0; queryIndex < queryState.queryIds.size(); ++queryIndex)
        {
            switch (stageIndex)
            {
            case kLightingSetupStage:
                ResolveGpuTiming(queryState, m_lightingGpuTiming.setupMs, m_lightingGpuTiming.hasSetupResult, queryIndex);
                break;
            case kLightingAmbientStage:
                ResolveGpuTiming(queryState, m_lightingGpuTiming.ambientMs, m_lightingGpuTiming.hasAmbientResult, queryIndex);
                break;
            case kLightingAccumulationStage:
                ResolveGpuTiming(queryState, m_lightingGpuTiming.lightAccumulationMs, m_lightingGpuTiming.hasLightAccumulationResult, queryIndex);
                break;
            default:
                break;
            }
        }
    }

    void Renderer::ResolveAllPostProcessGpuTimings()
    {
        if (!m_gpuProfilingSupported)
        {
            return;
        }

        for (std::size_t timingIndex = 0; timingIndex < m_postProcessGpuTimerQueries.size(); ++timingIndex)
        {
            ResolveAllPostProcessGpuTimings(timingIndex);
        }

        UpdatePostProcessGpuTimingTotal();
    }

    void Renderer::UpdatePostProcessGpuTimingTotal()
    {
        float totalPostProcessTimeMs = 0.0f;
        bool hasPostProcessResult = false;
        for (const auto &postProcessGpuTiming : m_postProcessGpuTimings)
        {
            if (!postProcessGpuTiming.hasResult)
                continue;

            totalPostProcessTimeMs += postProcessGpuTiming.gpuTimeMs;
            hasPostProcessResult = true;
        }

        for (auto &gpuPassTiming : m_gpuPassTimings)
        {
            if (gpuPassTiming.name != "Post Process")
            {
                continue;
            }

            gpuPassTiming.gpuTimeMs = totalPostProcessTimeMs;
            gpuPassTiming.hasResult = hasPostProcessResult;
            break;
        }
    }

    void Renderer::ResolveAllPostProcessGpuTimings(std::size_t timingIndex)
    {
        if (!m_gpuProfilingSupported || timingIndex >= m_postProcessGpuTimerQueries.size())
        {
            return;
        }

        bool resolvedResult = false;
        for (std::size_t queryIndex = 0; queryIndex < m_postProcessGpuTimerQueries[timingIndex].queryIds.size(); ++queryIndex)
        {
            ResolveGpuTiming(
                m_postProcessGpuTimerQueries[timingIndex],
                m_postProcessGpuTimings[timingIndex].gpuTimeMs,
                resolvedResult,
                queryIndex);
        }
        if (resolvedResult)
            m_postProcessGpuTimingHasCachedResult[timingIndex] = true;
    }

    std::size_t Renderer::EnsurePostProcessGpuTiming(std::string_view effectName)
    {
        const std::string timingName = "Post Process / " + std::string(effectName);
        const auto existingIt = m_postProcessGpuTimingIndices.find(timingName);
        if (existingIt != m_postProcessGpuTimingIndices.end())
        {
            return existingIt->second;
        }

        const std::size_t timingIndex = m_postProcessGpuTimings.size();
        m_postProcessGpuTimingIndices.emplace(timingName, timingIndex);
        m_postProcessGpuTimings.push_back(GpuPassTiming{timingName});
        m_postProcessGpuTimingHasCachedResult.push_back(false);
        auto &queryState = m_postProcessGpuTimerQueries.emplace_back();
        glGenQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
        return timingIndex;
    }

    void Renderer::ResolveGpuTiming(std::size_t timingIndex, std::size_t queryIndex)
    {
        if (!m_gpuProfilingSupported || timingIndex >= m_gpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_gpuTimerQueries[timingIndex];
        ResolveGpuTiming(queryState, m_gpuPassTimings[timingIndex].gpuTimeMs, m_gpuPassTimings[timingIndex].hasResult, queryIndex);
    }

    void Renderer::ResolveGpuTiming(GpuTimerQueryState &queryState, float &gpuTimeMs, bool &hasResult, std::size_t queryIndex)
    {
        if (!queryState.pending[queryIndex])
        {
            return;
        }

        GLuint isAvailable = GL_FALSE;
        glGetQueryObjectuiv(queryState.queryIds[queryIndex], GL_QUERY_RESULT_AVAILABLE, &isAvailable);
        if (isAvailable == GL_FALSE)
        {
            return;
        }

        GLuint64 elapsedNanoseconds = 0;
        glGetQueryObjectui64v(queryState.queryIds[queryIndex], GL_QUERY_RESULT, &elapsedNanoseconds);
        gpuTimeMs = static_cast<float>(elapsedNanoseconds) * kNanosecondsToMilliseconds;
        hasResult = true;
        queryState.pending[queryIndex] = false;
    }
}
