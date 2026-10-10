#include "PlutoGE/render/RhiOcean.h"
#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include "BasicDrawBatching.h"
#include "CanonicalGeometry.h"
#include "ParticleVisibility.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RhiPostProcessAdapter.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "RhiDrawPreparationCache.h"
#include "PlutoGE/render/VctSceneSelection.h"
#include "RhiSkinning.h"
#include "rhi/NormalMipmaps.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numeric>
#include <tuple>

namespace PlutoGE::render
{
    namespace
    {
        std::atomic_uint64_t g_nextUpscalerContextId{1};

        float Halton(std::uint64_t index, std::uint32_t base)
        {
            float result = 0.0f;
            float fraction = 1.0f;
            while (index != 0)
            {
                fraction /= static_cast<float>(base);
                result += fraction * static_cast<float>(index % base);
                index /= base;
            }
            return result;
        }

        std::array<float, 16> RowMajor(const glm::mat4 &matrix)
        {
            std::array<float, 16> result{};
            for (std::size_t row = 0; row < 4; ++row)
                for (std::size_t column = 0; column < 4; ++column)
                    result[row * 4 + column] = matrix[column][row];
            return result;
        }

        struct DirectionalShadowProjection
        {
            glm::mat4 matrix{1.0f};
            float worldTexelSize = 0.0f;
            float depthRange = 1.0f;
        };

        DirectionalShadowProjection BuildDirectionalShadowProjection(const CameraData &camera,
                                                                     const BasicLighting &lighting,
                                                                     float cascadeNear,
                                                                     float shadowDistance,
                                                                     float casterDistance,
                                                                     std::uint32_t shadowResolution)
        {
            const glm::mat4 inverseView = glm::inverse(camera.view);
            const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
            const glm::vec3 right = glm::normalize(glm::vec3(inverseView[0]));
            const glm::vec3 up = glm::normalize(glm::vec3(inverseView[1]));
            const glm::vec3 forward = -glm::normalize(glm::vec3(inverseView[2]));
            const float nearDistance = std::max(cascadeNear, camera.nearPlane);
            const float farDistance = std::max(shadowDistance, nearDistance + 0.01f);
            const float projectionX = std::max(std::abs(camera.projection[0][0]), 0.0001f);
            const float projectionY = std::max(std::abs(camera.projection[1][1]), 0.0001f);
            // Use actual camera coverage, matching the legacy cascade fit.
            // Radius quantization and world-anchored snapping still stabilize
            // camera translation and rotation at a fixed projection.
            const float inverseProjectionY = 1.0f / projectionY;
            const float inverseProjectionX = 1.0f / projectionX;

            std::array<glm::vec3, 8> corners{};
            std::size_t cornerIndex = 0;
            for (const float distance : {nearDistance, farDistance})
            {
                const glm::vec3 center = cameraPosition + forward * distance;
                const float coverageDepth = std::abs(camera.projection[3][3]) > 0.5f ? 1.0f : distance;
                const glm::vec3 horizontal = right * (coverageDepth * inverseProjectionX);
                const glm::vec3 vertical = up * (coverageDepth * inverseProjectionY);
                corners[cornerIndex++] = center - horizontal - vertical;
                corners[cornerIndex++] = center + horizontal - vertical;
                corners[cornerIndex++] = center - horizontal + vertical;
                corners[cornerIndex++] = center + horizontal + vertical;
            }

            glm::vec3 center(0.0f);
            for (const auto &corner : corners)
                center += corner;
            center /= static_cast<float>(corners.size());
            float radius = 0.0f;
            for (const auto &corner : corners)
                radius = std::max(radius, glm::length(corner - center));
            radius = std::ceil(std::max(radius, 0.1f) * 16.0f) / 16.0f;
            glm::vec3 lightDirection = lighting.directionalDirection;
            if (glm::dot(lightDirection, lightDirection) < 0.000001f)
                lightDirection = {0.4f, -0.8f, 0.3f};
            lightDirection = glm::normalize(lightDirection);
            const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.98f
                                          ? glm::vec3(0.0f, 0.0f, 1.0f)
                                          : glm::vec3(0.0f, 1.0f, 0.0f);
            const float lightOffset = std::max(casterDistance, farDistance) + radius + 1.0f;
            const glm::mat4 lightView = glm::lookAtRH(center - lightDirection * lightOffset, center, lightUp);
            const glm::vec3 lightSpaceCenter = glm::vec3(lightView * glm::vec4(center, 1.0f));
            // Tent support plus half a texel for world-grid snapping.
            const float guardTexels = std::clamp(lighting.shadowSoftness, 0.0f, 4.0f) + 1.5f;
            const float guard = guardTexels * (radius * 2.0f / std::max(shadowResolution, 1u)) + 0.01f;
            glm::vec3 minimum = lightSpaceCenter - glm::vec3(radius + guard, radius + guard, radius);
            glm::vec3 maximum = lightSpaceCenter + glm::vec3(radius + guard, radius + guard, radius);
            // The receiver slice alone is not a sufficient shadow-caster
            // volume. A directional-light caster may be outside the camera
            // slice on the light-facing side while its projection still
            // reaches a receiver inside it. Extrude toward the light (the
            // negative light direction), and include the full caster sphere.
            // Extending minimum.z instead grows the volume downstream and
            // makes valid casters cross the near plane as the camera rotates.
            const float casterExtrusion = std::max(casterDistance, farDistance);
            const glm::vec3 extrudedCenter = center - lightDirection * casterExtrusion;
            const glm::vec3 lightSpaceExtrudedCenter =
                glm::vec3(lightView * glm::vec4(extrudedCenter, 1.0f));
            minimum.z = std::min(minimum.z, lightSpaceExtrudedCenter.z - radius - 0.01f);
            maximum.z = std::max(maximum.z, lightSpaceExtrudedCenter.z + radius + 0.01f);
            const glm::vec2 extent(maximum.x - minimum.x, maximum.y - minimum.y);
            const glm::vec2 texelSize = extent / static_cast<float>(std::max(shadowResolution, 1u));
            // Anchor the grid to absolute world zero, not the moving cascade
            // centre. This is the key phase-stability rule used by GLSL CSM.
            const glm::vec2 worldAnchor = glm::vec2(lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            const glm::vec2 snappedMinimum = worldAnchor -
                                             glm::round((worldAnchor - glm::vec2(minimum)) / texelSize) * texelSize;
            const glm::vec2 snapOffset = snappedMinimum - glm::vec2(minimum);
            minimum.x += snapOffset.x;
            maximum.x += snapOffset.x;
            minimum.y += snapOffset.y;
            maximum.y += snapOffset.y;
            const float nearPlane = std::max(-maximum.z, 0.01f);
            const float farPlane = std::max(-minimum.z, nearPlane + 0.01f);
            return {
                glm::orthoRH_ZO(minimum.x, maximum.x, minimum.y, maximum.y,
                                nearPlane, farPlane) * lightView,
                std::max(extent.x, extent.y) / static_cast<float>(std::max(shadowResolution, 1u)),
                farPlane - nearPlane,
            };
        }
    }

    RhiSceneRenderer::RhiSceneRenderer() = default;
    RhiSceneRenderer::~RhiSceneRenderer() = default;
    bool RhiSceneRenderer::Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders)
    {
        Shutdown();
        auto renderer = std::make_unique<BasicRenderer>();
        if (!renderer->Initialize(device, shaders))
            return false;
        renderer->SetSubmissionLabel(m_submissionLabel);
        m_device = &device;
        // Weak ownership prevents cached resources outliving their device. All
        // renderers are initialized and used on the rendering thread.
        static std::unordered_map<rhi::IRenderDevice *, std::weak_ptr<TextureCache>> caches;
        std::erase_if(caches, [](const auto &entry) { return entry.second.expired(); });
        m_textureCache = caches[&device].lock();
        if (!m_textureCache)
            caches[&device] = m_textureCache = std::make_shared<TextureCache>();
        m_renderer = std::move(renderer);
        m_upscalerContextId = g_nextUpscalerContextId.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void RhiSceneRenderer::SetSubmissionLabel(std::string label)
    {
        m_submissionLabel = std::move(label);
        if (m_renderer)
            m_renderer->SetSubmissionLabel(m_submissionLabel);
    }

    void RhiSceneRenderer::ReuseSkinningForFrame(const RhiSceneRenderer &source)
    {
        if (this == &source || !m_device || m_device != source.m_device || source.m_skinningFrame == 0)
            throw std::invalid_argument("Skinning reuse requires a rendered source on the same device");
        m_skinningCache = source.m_skinningCache;
        m_borrowedSkinningCache = true;
        m_reusedSkinningFrame = std::pair{source.m_skinningFrame, source.m_skinningHistoryEpoch};
    }

    void RhiSceneRenderer::InvalidateAssetCache()
    {
        if (m_normalMipJob.valid())
            m_normalMipJob.wait();
        m_normalMipJob = {};
        m_pendingNormalSource = nullptr;
        if (m_textureCache)
        {
            m_textureCache->srgb.clear();
            m_textureCache->linear.clear();
            m_textureCache->normal.clear();
            m_textureCache->versions.clear();
            ++m_textureCache->residencyRevision;
        }
        if (m_drawPreparation)
            m_drawPreparation->Reset();
        m_iblCaptures.clear();
        m_meshes.clear();
        m_skinningCache = std::make_shared<SkinningCache>();
        m_reusedSkinningFrame.reset();
        m_borrowedSkinningCache = false;
    }

    void RhiSceneRenderer::Shutdown()
    {
        m_drawPreparation.reset();
        m_skinningExecutor.reset();
        m_particleDraws.clear();
        m_sortedParticles.clear();
        if (m_device && m_upscalerContextId != 0)
            m_device->ReleaseTemporalUpscalerContext(m_upscalerContextId);
        m_iblCaptures.clear();
        m_meshes.clear();
        m_skinningCache = std::make_shared<SkinningCache>();
        m_reusedSkinningFrame.reset();
        m_borrowedSkinningCache = false;
        m_textureCache.reset();
        // The worker owns its pixels and never touches scene or GPU objects.
        m_normalMipJob = {};
        m_pendingNormalSource = nullptr;
        if (m_renderer)
            m_renderer->Shutdown();
        m_renderer.reset();
        m_device = nullptr;
        m_sceneCommandCount = 0;
        m_drawCount = 0;
        m_temporalFrameIndex = 0;
        m_previousTemporalJitterNdc = glm::vec2(0.0f);
        m_previousUpscalerViewProjection = glm::mat4(1.0f);
        m_previousRenderSize = {};
        m_previousOutputSize = {};
        m_upscalerHistoryValid = false;
        m_upscalerStatus = {};
        m_upscalerContextId = 0;
    }

    bool RhiSceneRenderer::Render(std::uint32_t width, std::uint32_t height, const CameraData &cameraData,
                                  const BasicLighting &sourceLighting, RenderCommandView commands,
                                  RenderCommandView shadowCommands,
                                  std::span<IPostProcessEffect *const> postProcessEffects,
                                  std::span<const BasicPostProcessEffect> atmosphereEffects,
                                  const TexturePixelReader &texturePixelReader, PostProcessDebugView debugView,
                                  bool submit, const scene::Scene *scene,
                                  std::optional<std::span<scene::Light *const>> lights,
                                  const BasicRenderer::BeforeTemporalResolve &beforeTemporalResolve, bool linearOutput,
                                  std::optional<glm::vec2> sharedClipJitter,
                                  BasicRenderer::RecordingMode recordingMode)
    {
        BasicLighting effectiveLighting = sourceLighting;
        m_graphicsQuality.Apply(effectiveLighting);
        const auto &lighting = effectiveLighting;
        // A tag-filtered view supplies its own lights; otherwise every scene light applies.
        std::vector<scene::Light *> allSceneLights;
        if (!lights && scene)
            allSceneLights = scene->GetLights();
        const std::span<scene::Light *const> sceneLights = lights ? *lights : std::span<scene::Light *const>(allSceneLights);
        const bool hasSceneLights = scene || lights;
        core::CpuScope renderScope("RHI.Scene", core::CpuCategory::Rendering);
        core::CpuScope translationScope("Command translation", core::CpuCategory::Rendering);
        const auto totalStart = std::chrono::steady_clock::now();
        const auto millisecondsBetween = [](const auto start, const auto end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        };
        m_timingStats = {};
        if (!m_renderer || !m_device || width == 0 || height == 0)
            return false;
        m_renderer->SetTransparentBackground(m_transparentBackground);
        const rhi::Extent2D outputSize{width, height};
        const bool temporalUpscalerRequested = m_upscalerOptions.technology != rhi::TemporalUpscaler::None;
        const auto upscalerSupport = temporalUpscalerRequested
                                         ? m_device->GetTemporalUpscalerSupport(m_upscalerOptions.technology)
                                         : rhi::TemporalUpscalerSupport{};
        const bool orthographic = std::abs(cameraData.projection[3][3]) > 0.5f;
        const bool useTemporalUpscaler = temporalUpscalerRequested && upscalerSupport.supported;
        const rhi::Extent2D renderSize = useTemporalUpscaler
            ? m_device->GetOptimalRenderSize(m_upscalerOptions, outputSize)
            : outputSize;
        if (renderSize.width == 0 || renderSize.height == 0)
            return false;
        m_upscalerStatus = {
            .options = m_upscalerOptions,
            .renderSize = renderSize,
            .outputSize = outputSize,
            .requested = temporalUpscalerRequested,
            .active = false,
            .reason = useTemporalUpscaler ? std::string{} : upscalerSupport.reason,
        };
        const bool projectionChanged = orthographic != m_previousUpscalerOrthographic;
        m_previousUpscalerOrthographic = orthographic;
        const bool resolutionChanged = renderSize != m_previousRenderSize || outputSize != m_previousOutputSize || projectionChanged;
        if (resolutionChanged)
            ResetTemporalHistory();
        auto effectiveUpscaler = m_upscalerOptions;
        if (!useTemporalUpscaler)
            effectiveUpscaler.technology = rhi::TemporalUpscaler::None;
        m_renderer->SetTemporalUpscalerOptions(effectiveUpscaler);
        if (!m_renderer->Resize(renderSize.width, renderSize.height, outputSize.width, outputSize.height))
            return false;
        width = renderSize.width;
        height = renderSize.height;
        m_timingStats.translationPreparationMs = millisecondsBetween(totalStart, std::chrono::steady_clock::now());

        m_sceneCommandCount = commands.size();
        if (m_reusedSkinningFrame)
        {
            std::tie(m_skinningFrame, m_skinningHistoryEpoch) = *m_reusedSkinningFrame;
            m_reusedSkinningFrame.reset();
        }
        else
        {
            if (m_borrowedSkinningCache)
            {
                m_skinningCache = std::make_shared<SkinningCache>();
                m_borrowedSkinningCache = false;
            }
            ++m_skinningFrame;
        }
        auto &m_skinnedMeshes = m_skinningCache->meshes;
        std::erase_if(m_skinningCache->shadowBounds, [](const auto &entry) { return entry.second.lifetime.expired(); });
        if (!m_drawPreparation)
            m_drawPreparation = std::make_unique<RhiDrawPreparationCache>();
        auto &preparation = *m_drawPreparation;
        ++m_preparationFrame;
        preparation.BeginFrame(m_preparationFrame);
        std::erase_if(m_meshes, [&](const auto &entry) {
            if (!entry.second.lifetime.expired() && entry.second.contentRevision == entry.first->GetContentRevision()) return false;
            preparation.InvalidateMesh(entry.first);
            return true;
        });
        std::erase_if(preparation.materials,
                      [&](const auto &entry) { return entry.second.frame + 2 < m_preparationFrame; });
        // Retain offscreen poses briefly, but do not accumulate destroyed
        // animators indefinitely in scenes that spawn disposable characters.
        for (auto model = m_skinnedMeshes.begin(); model != m_skinnedMeshes.end(); )
        {
            std::erase_if(model->second, [&](const auto &entry) { return entry.second.lifetime.expired() || m_skinningFrame - entry.second.lastFrame > 120; });
            if (model->second.empty()) model = m_skinnedMeshes.erase(model);
            else ++model;
        }

        // Revisions are checked without reading texture pixels. Remove expired
        // keys before dereferencing them; identity also handles address reuse.
        std::erase_if(m_textureCache->versions, [&](const auto &item) {
            const auto *source = item.first;
            const auto &version = item.second;
            if (!version.lifetime.expired() && version.identity == source->GetIdentity() &&
                version.revision == source->GetContentRevision()) return false;
            m_textureCache->srgb.erase(source); m_textureCache->linear.erase(source); m_textureCache->normal.erase(source);
            ++m_textureCache->residencyRevision;
            return true;
        });
        if (m_normalMipJob.valid() &&
            m_normalMipJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto pixels = m_normalMipJob.get();
            const auto uploadStart = std::chrono::steady_clock::now();
            core::CpuScope uploadScope("Prepared normal texture upload", core::CpuCategory::Rendering);
            // Another view may have finished this image while our CPU job ran.
            // Never upload a duplicate, or publish work for an invalidated source.
            if (!m_pendingNormalLifetime.expired() &&
                m_pendingNormalSource->GetContentRevision() == m_pendingNormalRevision &&
                !m_textureCache->normal.contains(m_pendingNormalSource))
            {
                ++m_timingStats.textureUploadCount;
                rhi::Texture uploaded(*m_device, m_device->CreateTexture(
                    {m_pendingNormalWidth, m_pendingNormalHeight, rhi::Format::R8G8B8A8Unorm,
                     rhi::TextureUsage::Sampled, "Scene normal", false, 1, false, 0, true, true}, pixels));
                if (uploaded)
                {
                    m_textureCache->normal.emplace(m_pendingNormalSource, std::move(uploaded));
                    ++m_textureCache->residencyRevision;
                }
            }
            m_timingStats.textureUploadMs += millisecondsBetween(uploadStart, std::chrono::steady_clock::now());
            m_pendingNormalSource = nullptr;
        }
        const auto uploadTexture = [&](const Texture *source, rhi::Format format,
                                       auto &cache,
                                       const char *debugName, bool normalMap = false) -> rhi::TextureHandle
        {
            if (!source || source->GetWidth() <= 0 || source->GetHeight() <= 0)
                return {};
            m_textureCache->versions.try_emplace(source, TextureVersion{source->GetLifetimeToken(), source->GetIdentity(), source->GetContentRevision()});
            // Render textures are drawn on the GPU; republishing bumps their
            // revision, which re-prepares the materials that sample them.
            if (const auto *renderTexture = dynamic_cast<const RenderTexture *>(source))
                return renderTexture->GetGpuTexture(*m_device);
            if (!texturePixelReader)
                return {};
            if (const auto cached = cache.find(source); cached != cache.end())
                return cached->second.Get();
            if (normalMap && m_normalMipJob.valid())
                return {};
            core::CpuScope readScope("Texture pixel read", core::CpuCategory::Rendering);
            const auto readStart = std::chrono::steady_clock::now();
            auto pixels = texturePixelReader(*source);
            m_timingStats.textureReadMs += millisecondsBetween(readStart, std::chrono::steady_clock::now());
            readScope.End();
            const auto expectedSize = static_cast<std::size_t>(source->GetWidth()) * source->GetHeight() * 4;
            if (pixels.size() != expectedSize)
                return {};
            if (normalMap && m_immediateTextureUploads)
            {
                const auto width = static_cast<std::uint32_t>(source->GetWidth());
                const auto height = static_cast<std::uint32_t>(source->GetHeight());
                const auto levels = 1u + static_cast<unsigned>(std::floor(std::log2(std::max(width, height))));
                pixels = rhi::BuildNormalMipmaps(pixels, width, height, levels);
                rhi::Texture uploaded(*m_device,
                                      m_device->CreateTexture({width, height, format, rhi::TextureUsage::Sampled,
                                                               debugName, false, 1, false, 0, true, true},
                                                              pixels));
                return uploaded ? cache.emplace(source, std::move(uploaded)).first->second.Get() : rhi::TextureHandle{};
            }
            if (normalMap)
            {
                const auto width = static_cast<std::uint32_t>(source->GetWidth());
                const auto height = static_cast<std::uint32_t>(source->GetHeight());
                m_pendingNormalSource = source;
                m_pendingNormalLifetime = source->GetLifetimeToken();
                m_pendingNormalRevision = source->GetContentRevision();
                m_pendingNormalWidth = width;
                m_pendingNormalHeight = height;
                m_normalMipJob = std::async(std::launch::async,
                    [pixels = std::move(pixels), width, height]() {
                        const auto levels = 1u + static_cast<unsigned>(std::floor(std::log2(std::max(width, height))));
                        return rhi::BuildNormalMipmaps(pixels, width, height, levels);
                    });
                return {};
            }
            core::CpuScope uploadScope("Texture creation and mipmaps", core::CpuCategory::Rendering);
            const auto uploadStart = std::chrono::steady_clock::now();
            ++m_timingStats.textureUploadCount;
            rhi::Texture uploaded(*m_device, m_device->CreateTexture(
                                                 {static_cast<std::uint32_t>(source->GetWidth()), static_cast<std::uint32_t>(source->GetHeight()),
                                                  format, rhi::TextureUsage::Sampled, debugName, false, 1, false, 0, normalMap},
                                                 pixels));
            m_timingStats.textureUploadMs += millisecondsBetween(uploadStart, std::chrono::steady_clock::now());
            return uploaded ? cache.emplace(source, std::move(uploaded)).first->second.Get()
                            : rhi::TextureHandle{};
        };

        // Direct config edits are legal. Validate each distinct material once
        // per frame, then use its revision for every list and batching key.
        const auto prepareMaterial = [&](const Material *source) -> const RhiDrawPreparationCache::MaterialEntry & {
            auto &entry = preparation.materials[source];
            if (entry.frame == m_preparationFrame)
                return entry;
            const auto sourceRevision = source->GetRevision();
            if (sourceRevision && entry.sourceIdentity == source->GetIdentity() &&
                entry.sourceRevision == sourceRevision && entry.textureResidencyRevision == m_textureCache->residencyRevision &&
                !m_normalMipJob.valid())
            {
                entry.frame = m_preparationFrame;
                return entry;
            }
            BasicDraw draw;
            const auto &material = source->ReadConfig();
            draw.baseColor = material.color;
            draw.uvScale = material.uvScale;
            draw.metallic = material.metallic;
            draw.roughness = material.roughness;
            draw.emission = material.emission;
            draw.emissionTexCoord = material.emissionTexCoord;
            draw.emissionChannelMask = material.emissionChannelMask;
            draw.emissionChannels = material.emissionChannels;
            draw.emissionTexture = material.emissionChannelMask
                ? uploadTexture(material.emissionTexture, rhi::Format::R8G8B8A8Unorm, m_textureCache->linear, "Emission masks")
                : uploadTexture(material.emissionTexture, rhi::Format::R8G8B8A8Srgb, m_textureCache->srgb, "Scene emission");
            draw.subsurface = material.subsurface;
            draw.subsurfaceColor = material.subsurfaceColor;
            draw.subsurfaceRadius = material.subsurfaceRadius;
            draw.surfaceType = static_cast<std::uint32_t>(material.surfaceType);
            draw.transmission = material.transmission;
            draw.ior = material.ior;
            draw.thickness = material.thickness;
            draw.attenuationColor = material.attenuationColor;
            draw.attenuationDistance = material.attenuationDistance;
            draw.twoSided = material.twoSided;
            draw.outlineWidth = material.outline.enabled ? material.outline.width : 0.0f;
            draw.outlineColor = material.outline.color;
            draw.shaderGraphProgram = material.shaderGraphProgram;
            draw.graphPassOrder = material.graphPassOrder;
            draw.graphSamplers = material.graphSamplers;
            for (size_t i = 0; i < draw.graphTextures.size(); ++i)
                draw.graphTextures[i] = uploadTexture(material.graphTextures[i], rhi::Format::R8G8B8A8Unorm,
                                                      m_textureCache->linear, "Graph texture");
            const bool transparent =
                material.surfaceType == MaterialSurfaceType::Glass || material.alphaMode == AlphaMode::Blend;
            draw.contributesToGi = !transparent;
            draw.castsShadow = material.castsShadow && !transparent;
            draw.alphaCutoff = material.alphaCutoff;
            draw.alphaMode = static_cast<std::uint32_t>(material.alphaMode);
            draw.metallicChannel = static_cast<std::uint32_t>(material.metallicTextureChannel);
            draw.roughnessChannel = static_cast<std::uint32_t>(material.roughnessTextureChannel);
            draw.flipNormalY = material.flipNormalY;
            draw.baseColorTexture =
                uploadTexture(material.albedoTexture, rhi::Format::R8G8B8A8Srgb, m_textureCache->srgb, "Scene albedo");
            draw.normalTexture = uploadTexture(material.normalTexture, rhi::Format::R8G8B8A8Unorm, m_textureCache->normal,
                                               "Scene normal", true);
            draw.metallicTexture =
                uploadTexture(material.metallicTexture, rhi::Format::R8G8B8A8Unorm, m_textureCache->linear, "Scene metallic");
            draw.roughnessTexture = uploadTexture(material.roughnessTexture, rhi::Format::R8G8B8A8Unorm,
                                                  m_textureCache->linear, "Scene roughness");
            if (entry.revision == 0 || !SameBasicDrawSurface(entry.draw, draw))
            {
                entry.revision = preparation.NextRevision();
                draw.preparedMaterialHash = BasicMaterialBatchHash(draw);
            }
            else
                draw.preparedMaterialHash = entry.draw.preparedMaterialHash;
            entry.draw = std::move(draw);
            entry.sourceIdentity = source->GetIdentity();
            const bool pendingTexture = (material.albedoTexture && !entry.draw.baseColorTexture) ||
                (material.normalTexture && !entry.draw.normalTexture) || (material.metallicTexture && !entry.draw.metallicTexture) ||
                (material.roughnessTexture && !entry.draw.roughnessTexture) || (material.emissionTexture && !entry.draw.emissionTexture);
            bool pendingGraphTexture = false;
            for (std::size_t i = 0; i < material.graphTextures.size(); ++i)
                pendingGraphTexture |= material.graphTextures[i] && !entry.draw.graphTextures[i];
            entry.sourceRevision = pendingTexture || pendingGraphTexture ? 0 : sourceRevision;
            entry.textureResidencyRevision = m_textureCache->residencyRevision;
            entry.frame = m_preparationFrame;
            return entry;
        };

        const bool localShadows = hasSceneLights
            ? std::ranges::any_of(sceneLights, [](const auto *light) {
                return light && light->type != scene::LightType::Directional && light->castsShadows;
            })
            : std::ranges::any_of(lighting.spotLights, [](const auto &spot) { return spot.light.castsShadows; }) ||
              std::ranges::any_of(lighting.pointLights, [](const auto &light) { return light.castsShadows; });
        auto &pendingSkinning = m_pendingSkinning;
        auto &skinningJobs = m_skinningJobs;
        pendingSkinning.clear();
        skinningJobs.clear();
        const auto collectSkinning = [&](RenderCommandView sources, bool shadowOnly)
        {
            for (const auto &command : sources)
            {
                if (!command.mesh || !command.jointMatrices || command.jointMatrices->empty() ||
                    (shadowOnly && (!command.castsShadow ||
                        (command.material && !prepareMaterial(command.material).draw.castsShadow)))) continue;
                const auto &source = command.mesh->GetMeshData();
                if (source.vertices.empty() || source.indices.empty()) continue;
                auto &entry = m_skinnedMeshes[command.mesh][command.jointMatrices];
                if (entry.lastFrame == m_skinningFrame || entry.queuedFrame == m_skinningFrame) continue;
                entry.lifetime = command.mesh->GetLifetimeToken();
                const bool topologyChanged = entry.sourceVertexCount != source.vertices.size() || entry.mesh.GetIndexCount() != source.indices.size() ||
                    entry.contentRevision != command.mesh->GetContentRevision();
                const bool changed = entry.pose != *command.jointMatrices || topologyChanged ||
                    entry.contentRevision != command.mesh->GetContentRevision();
                const bool hasHistory = entry.lastFrame + 1 == m_skinningFrame && entry.historyEpoch == m_skinningHistoryEpoch;
                const bool deform = changed || !entry.mesh.IsValid();
                const bool upload = deform || entry.wasMoving || !hasHistory;
                if (!entry.shadowBounds || topologyChanged || entry.shadowBounds->GetJointCount() != command.jointMatrices->size())
                {
                    auto &cached = m_skinningCache->shadowBounds[command.mesh];
                    if (!cached.bounds || cached.lifetime.expired() || cached.contentRevision != command.mesh->GetContentRevision() ||
                        cached.bounds->GetJointCount() != command.jointMatrices->size())
                    {
                        cached.bounds = std::make_shared<RhiSkinnedShadowBounds>();
                        cached.gpuSource.reset();
                        cached.bounds->Build(source.vertices, source.indices, command.jointMatrices->size());
                        cached.contentRevision = command.mesh->GetContentRevision();
                        cached.lifetime = command.mesh->GetLifetimeToken();
                    }
                    entry.shadowBounds = cached.bounds;
                    if (m_renderer->SupportsGpuSkinning())
                    {
                        if (!cached.gpuSource) cached.gpuSource = m_renderer->CreateGpuSkinningSource(source.vertices);
                        entry.gpuSource = cached.gpuSource;
                    }
                    entry.shadowClusters.clear();
                }
                const auto jobIndex = skinningJobs.size();
                if (deform && !entry.gpuSource)
                    skinningJobs.push_back({source.vertices, *command.jointMatrices,
                        hasHistory ? std::span<const BasicVertex>(*entry.vertices) : std::span<const BasicVertex>{}, entry.vertices.get()});
                else if (upload && !entry.gpuSource)
                    for (auto &vertex : *entry.vertices)
                        vertex.previousPosition = {vertex.position[0], vertex.position[1], vertex.position[2], 1};
                pendingSkinning.push_back({&entry, command.mesh, command.jointMatrices, changed, topologyChanged, upload, hasHistory,
                    deform && !entry.gpuSource ? jobIndex : std::numeric_limits<std::size_t>::max()});
                // Claim this mesh/pose once across all submeshes and pass lists.
                // No draw consumes it until flushSkinning has joined and uploaded.
                entry.queuedFrame = m_skinningFrame;
            }
        };
        const auto flushSkinning = [&]
        {
            if (pendingSkinning.empty()) return;
            const auto start = std::chrono::steady_clock::now();
            if (!skinningJobs.empty())
            {
                core::CpuScope skinScope("Skeletal vertex deformation", core::CpuCategory::Rendering);
                std::size_t vertices = 0;
                for (const auto &job : skinningJobs) vertices += job.source.size();
                if (!m_skinningExecutor && vertices >= RhiSkinningExecutor::MinimumParallelVertices)
                    m_skinningExecutor = std::make_unique<RhiSkinningExecutor>();
                if (m_skinningExecutor)
                {
                    m_skinningExecutor->DeformBatch(skinningJobs);
                    const auto &work = m_skinningExecutor->stats;
                    m_timingStats.skinningParticipants = std::max(m_timingStats.skinningParticipants, work.participants);
                    m_timingStats.skinningDispatchMs += work.dispatchMs;
                    m_timingStats.skinningCallerMs += work.callerMs;
                    m_timingStats.skinningWaitMs += work.waitMs;
                    m_timingStats.skinningMergeMs += work.mergeMs;
                }
                else
                    for (auto &job : skinningJobs)
                        job.bounds = SkinRhiVerticesInto(job.source, job.joints, job.previous, *job.output);
                m_timingStats.skinningDeformationMs += millisecondsBetween(start, std::chrono::steady_clock::now());
            }
            for (const auto &pending : pendingSkinning)
            {
                auto &entry = *pending.entry;
                if (entry.gpuSource)
                {
                    if (pending.upload)
                    {
                        core::CpuScope gpuScope("Skeletal GPU preparation", core::CpuCategory::Rendering);
                        entry.shadowBounds->Refit(*pending.pose, entry.shadowClusters);
                        const auto bounds = MergeShadowGeometryClusters(entry.shadowClusters);
                        entry.boundsCenter = bounds.center;
                        entry.boundsExtents = bounds.extents;
                        entry.boundsRadius = glm::all(glm::greaterThanEqual(bounds.extents, glm::vec3(0))) ? glm::length(bounds.extents) : -1;
                        if (!entry.mesh.IsValid() || pending.topologyChanged)
                        {
                            entry.mesh = m_renderer->CreateGpuSkinnedMesh(entry.gpuSource, pending.mesh->GetMeshData().indices, entry.shadowClusters);
                            ++m_timingStats.meshUploadCount;
                        }
                        const auto previous = pending.hasHistory && !pending.topologyChanged ? std::span<const glm::mat4>(entry.pose) : std::span<const glm::mat4>(*pending.pose);
                        // A changed palette size also resets history.
                        m_renderer->UpdateGpuSkinnedMesh(entry.mesh, *pending.pose,
                            previous.size() == pending.pose->size() ? previous : std::span<const glm::mat4>(*pending.pose),
                            pending.changed, entry.shadowClusters);
                        if (pending.changed)
                        {
                            ++m_timingStats.skinningUpdateCount;
                            m_timingStats.skinningVertexCount += pending.mesh->GetMeshData().vertices.size();
                        }
                    }
                    const bool movingHistory = pending.changed && pending.hasHistory && !pending.topologyChanged &&
                                               entry.pose.size() == pending.pose->size();
                    entry.pose = *pending.pose;
                    entry.sourceVertexCount = pending.mesh->GetMeshData().vertices.size();
                    entry.contentRevision = pending.mesh->GetContentRevision();
                    entry.wasMoving = movingHistory;
                    entry.lastFrame = m_skinningFrame;
                    entry.historyEpoch = m_skinningHistoryEpoch;
                    continue;
                }
                if (pending.jobIndex != std::numeric_limits<std::size_t>::max())
                {
                    const auto &job = skinningJobs[pending.jobIndex];
                    entry.boundsCenter = job.bounds.center;
                    entry.boundsExtents = job.bounds.extents;
                    entry.boundsRadius = job.bounds.radius;
                    entry.pose = *pending.pose;
                    ++m_timingStats.skinningUpdateCount;
                    m_timingStats.skinningVertexCount += job.source.size();
                }
                if (pending.upload)
                {
                    core::CpuScope uploadScope("Skeletal vertex upload", core::CpuCategory::Rendering);
                    const auto uploadStart = std::chrono::steady_clock::now();
                    if (!entry.mesh.IsValid() || pending.topologyChanged)
                    {
                        entry.mesh = m_renderer->CreateMesh({*entry.vertices, pending.mesh->GetMeshData().indices});
                        ++m_timingStats.meshUploadCount;
                    }
                    else
                    {
                        if (pending.changed)
                        {
                            core::CpuScope boundsScope("Skeletal shadow bounds refit", core::CpuCategory::Rendering);
                            entry.shadowBounds->Refit(*pending.pose, entry.shadowClusters);
                        }
                        m_renderer->UpdateSharedMeshVertices(entry.mesh, entry.vertices, pending.changed, entry.shadowClusters);
                    }
                    m_timingStats.skinningUploadMs += millisecondsBetween(uploadStart, std::chrono::steady_clock::now());
                }
                entry.contentRevision = pending.mesh->GetContentRevision();
                entry.sourceVertexCount = pending.mesh->GetMeshData().vertices.size();
                entry.wasMoving = pending.changed;
                // Commit history only after a successful upload. A recoverable
                // upload failure must force history reset on the next frame.
                entry.lastFrame = m_skinningFrame;
                entry.historyEpoch = m_skinningHistoryEpoch;
            }
            m_timingStats.meshUploadMs += millisecondsBetween(start, std::chrono::steady_clock::now());
            pendingSkinning.clear();
            skinningJobs.clear();
        };
        collectSkinning(commands, false);
        if (lighting.shadowsEnabled || localShadows) collectSkinning(shadowCommands, true);
        flushSkinning();

        const auto appendDraws = [&](RenderCommandView sourceCommands,
                                     RhiDrawPreparationCache::List &cache, bool shadowOnly, bool giOnly = false) {
            // GI may introduce a pose absent from the visible/shadow lists.
            // Visible and shadow poses were already collected together above.
            if (giOnly)
            {
                collectSkinning(sourceCommands, shadowOnly);
                flushSkinning();
            }
            const auto materialRevision = [&](const RenderCommand &command) {
                return command.material ? prepareMaterial(command.material).revision : std::uint64_t{0};
            };
            bool unchanged = cache.entries.size() == sourceCommands.size();
            for (size_t i = 0; i < sourceCommands.size() && unchanged; ++i)
                unchanged = cache.entries[i].Matches(sourceCommands[i], materialRevision(sourceCommands[i]));
            if (unchanged)
            {
                m_timingStats.reusedDrawPackets += sourceCommands.size();
                return false;
            }
            bool patch = cache.entries.size() == sourceCommands.size() && cache.draws.size() == sourceCommands.size();
            for (std::size_t i = 0; patch && i < sourceCommands.size(); ++i)
                patch = cache.entries[i].input.sourceObject == sourceCommands[i].sourceObject &&
                    cache.entries[i].input.mesh == sourceCommands[i].mesh &&
                    cache.entries[i].input.material == sourceCommands[i].material &&
                    cache.entries[i].input.submeshIndex == sourceCommands[i].submeshIndex;
            cache.Reconcile(sourceCommands, materialRevision);
            auto &destination = cache.draws;
            if (!patch) destination.clear();
            destination.reserve(sourceCommands.size());
            for (size_t i = 0; i < sourceCommands.size(); ++i)
            {
                const auto &command = sourceCommands[i];
                const auto revision = materialRevision(command);
                auto &entry = cache.entries[i];
                if (entry.Matches(command, revision))
                {
                    if (entry.emitted && !patch)
                        destination.push_back(entry.draw);
                    ++m_timingStats.reusedDrawPackets;
                    continue;
                }
                entry.valid = false;
                entry.emitted = false;
                if (!command.mesh ||
                    (shadowOnly && (!command.castsShadow ||
                                    (command.material && !prepareMaterial(command.material).draw.castsShadow))))
                {
                    entry.Store(command, revision, nullptr);
                    continue;
                }
                const bool emissiveGi = giOnly && command.lodIndex != 0 && command.material &&
                    glm::any(glm::greaterThan(command.material->ReadConfig().emission, glm::vec3(0.0f)));
                if (const auto *shared = preparation.FindRetained(command, revision, emissiveGi, m_preparationFrame))
                {
                    entry = *shared;
                    if (patch) destination[i] = entry.draw; else destination.push_back(entry.draw);
                    ++m_timingStats.reusedDrawPackets;
                    ++m_timingStats.sharedDrawPacketHits;
                    continue;
                }
                BasicMesh *renderMesh = nullptr;
                CachedMesh *rigidMesh = nullptr;
                SkinnedMesh *deformed = nullptr;
                if (command.jointMatrices && !command.jointMatrices->empty())
                {
                    auto &entry = m_skinnedMeshes[command.mesh][command.jointMatrices];
                    deformed = &entry;
                    if (!entry.mesh.IsValid()) continue;
                    renderMesh = &entry.mesh;
                }
                else
                {
                    auto mesh = m_meshes.find(command.mesh);
                    if (mesh == m_meshes.end())
                    {
                        core::CpuScope meshScope("Mesh conversion and upload", core::CpuCategory::Rendering);
                        const auto &source = command.mesh->GetMeshData();
                        if (source.vertices.empty() || source.indices.empty())
                            continue;
                        const auto meshStart = std::chrono::steady_clock::now();
                        ++m_timingStats.meshUploadCount;
                        std::vector<BasicVertex> vertices;
                        vertices.reserve(source.vertices.size());
                        for (const auto &vertex : source.vertices)
                            vertices.push_back({vertex.position, vertex.normal, vertex.uv, vertex.tangent, {}, vertex.uv2});
                        std::vector<GeometryRange> ranges;
                        for (size_t submesh = 0; submesh < command.mesh->GetSubmeshCount(); ++submesh)
                            for (size_t lod = 0; lod < command.mesh->GetSubmeshLodCount(submesh); ++lod)
                            {
                                const auto range = command.mesh->GetSubmeshLodRange(submesh, lod);
                                ranges.push_back(
                                    {range.indexOffset, range.indexCount,
                                     (std::uint64_t(command.mesh->GetSubmesh(submesh).materialIndex) << 32) | lod});
                            }
                        auto packed = PackGeometryRanges({vertices, source.indices}, ranges);
                        mesh = m_meshes
                                   .emplace(command.mesh, CachedMesh{command.mesh->GetLifetimeToken(),
                                                                     m_renderer->CreateMesh({vertices, packed.indices}),
                                                                     std::move(packed.firstIndices), {}, command.mesh->GetContentRevision()})
                                   .first;
                        m_timingStats.meshUploadMs += millisecondsBetween(meshStart, std::chrono::steady_clock::now());
                    }
                    renderMesh = &mesh->second.mesh;
                    rigidMesh = &mesh->second;
                }

                std::uint32_t firstIndex = 0;
                std::uint32_t indexCount = static_cast<std::uint32_t>(command.mesh->GetMeshData().indices.size());
                if (command.submeshIndex < command.mesh->GetSubmeshCount())
                {
                    // Small emissive submeshes must not disappear from the GI
                    // source when the camera selects simplified geometry.
                    const auto range = command.mesh->GetSubmeshLodRange(command.submeshIndex, emissiveGi ? 0u : command.lodIndex);
                    firstIndex = range.indexOffset;
                    indexCount = range.indexCount;
                }
                if (rigidMesh)
                    if (const auto canonical = rigidMesh->canonicalGeometry.find(GeometryRangeKey({firstIndex, indexCount}));
                        canonical != rigidMesh->canonicalGeometry.end())
                        firstIndex = canonical->second;
                BasicDraw draw = command.material ? prepareMaterial(command.material).draw : BasicDraw{};
                draw.mesh = renderMesh;
                draw.model = command.model;
                draw.castsShadow = draw.castsShadow && command.castsShadow;
                draw.shadowBoundsCenter = command.worldBounds.center;
                draw.shadowBoundsRadius = command.worldBounds.radius;
                draw.firstIndex = firstIndex;
                draw.indexCount = indexCount;
                if (!deformed && (!command.jointMatrices || command.jointMatrices->empty()) &&
                    !command.instanceModels && command.submeshIndex < command.mesh->GetSubmeshCount())
                {
                    const auto &submesh = command.mesh->GetSubmesh(command.submeshIndex);
                    if (submesh.hasBoundsExtents)
                        OcclusionCulling::SetRigidBounds(draw, submesh.boundsMin, submesh.boundsMax);
                    else
                    {
                        // Imported ranges lacking an AABB can still use the
                        // conservative CPU geometry bounds retained by BasicMesh.
                        const auto available = firstIndex < renderMesh->GetIndexCount() ? renderMesh->GetIndexCount() - firstIndex : 0;
                        const auto count = std::min(indexCount ? indexCount : available, available);
                        auto [cached, inserted] = rigidMesh->localBounds.try_emplace(GeometryRangeKey({firstIndex, count}));
                        if (inserted)
                            cached->second = MergeShadowGeometryClusters(SelectShadowGeometryClusters(renderMesh->GetShadowClusters(), firstIndex, count));
                        const auto &bounds = cached->second;
                        if (glm::all(glm::greaterThanEqual(bounds.extents, glm::vec3(0))))
                            OcclusionCulling::SetRigidBounds(draw, bounds.center - bounds.extents, bounds.center + bounds.extents);
                    }
                }
                if (deformed)
                {
                    OcclusionCulling::SetLocalBounds(draw, deformed->boundsCenter - deformed->boundsExtents,
                                                    deformed->boundsCenter + deformed->boundsExtents);
                    draw.shadowBoundsCenter = glm::vec3(command.model * glm::vec4(deformed->boundsCenter, 1));
                    draw.shadowBoundsRadius = deformed->boundsRadius * std::max({glm::length(glm::vec3(command.model[0])), glm::length(glm::vec3(command.model[1])), glm::length(glm::vec3(command.model[2]))});
                }
                const std::size_t lodCount = command.submeshIndex < command.mesh->GetSubmeshCount()
                                                 ? command.mesh->GetSubmeshLodCount(command.submeshIndex)
                                                 : 1u;
                draw.normalizedLod = lodCount > 1
                                         ? static_cast<float>(command.lodIndex) /
                                               static_cast<float>(lodCount - 1)
                                         : 0.0f;
                // Scene imports commonly leave rigid architectural submeshes marked non-static.
                // VCT content signatures already invalidate moved rigid draws, so the Static flag
                // is a caching hint rather than a requirement for contributing to GI. Skinned
                // geometry remains excluded until the voxelizer supports joint transforms.
                draw.contributesToGi = draw.contributesToGi && (!command.jointMatrices || command.jointMatrices->empty());
                if (draw.shaderGraphProgram && draw.shaderGraphProgram->data.header.z > 0)
                    draw.shadowBoundsRadius = -1.0f;
                draw.previousModel = command.previousModel;
                draw.instanceModels = command.instanceModels;
                draw.previousInstanceModels = command.previousInstanceModels;
                draw.preparationRevision =
                    !command.jointMatrices && !command.instanceModels && !command.previousInstanceModels
                        ? preparation.NextRevision()
                        : 0;
                if (!command.material)
                    draw.preparedMaterialHash = BasicMaterialBatchHash(draw);
                entry.Store(command, revision, &draw);
                preparation.Retain(entry, emissiveGi, m_preparationFrame);
                if (patch) destination[i] = std::move(draw); else destination.push_back(std::move(draw));
                ++m_timingStats.rebuiltDrawPackets;
            }
            if (patch && std::ranges::any_of(cache.entries, [](const auto &entry) { return !entry.emitted; }))
            {
                destination.clear();
                for (const auto &entry : cache.entries) if (entry.emitted) destination.push_back(entry.draw);
            }
            return true;
        };
        const auto visibleStart = std::chrono::steady_clock::now();
        core::CpuScope visibleScope("Visible packet preparation", core::CpuCategory::Rendering);
        const bool visibleChanged = appendDraws(commands, preparation.visible, false);
        visibleScope.End();
        const auto batchingStart = std::chrono::steady_clock::now();
        m_timingStats.visiblePreparationMs = millisecondsBetween(visibleStart, batchingStart);
        core::CpuScope batchingScope("Opaque packet batching", core::CpuCategory::Rendering);
        if (visibleChanged)
        {
            const auto nextRevision = [&] { return preparation.NextRevision(); };
            const auto batches = preparation.opaqueBatches.Update(preparation.visible.draws, preparation.batched, nextRevision);
            m_timingStats.reusedOpaqueBatchGroups = batches.reused;
            m_timingStats.rebuiltOpaqueBatchGroups = batches.rebuilt;
        }
        auto &draws = preparation.batched;
        batchingScope.End();
        const auto shadowStart = std::chrono::steady_clock::now();
        m_timingStats.batchingMs = millisecondsBetween(batchingStart, shadowStart);
        core::CpuScope shadowPacketScope("Shadow packet preparation", core::CpuCategory::Rendering);
        if (lighting.shadowsEnabled || localShadows)
            appendDraws(shadowCommands, preparation.shadows, true);
        else
            preparation.shadows = {};
        auto &shadowDraws = preparation.shadows.draws;
        shadowPacketScope.End();
        m_timingStats.shadowPreparationMs = millisecondsBetween(shadowStart, std::chrono::steady_clock::now());
        translationScope.End();
        const auto translationEnd = std::chrono::steady_clock::now();
        core::CpuScope setupScope("Scene setup", core::CpuCategory::Rendering);
        m_timingStats.commandTranslationMs = millisecondsBetween(totalStart, translationEnd);
        m_timingStats.visibleDrawCount = draws.size();
        m_timingStats.visibleInstanceCount = std::accumulate(
            draws.begin(), draws.end(), std::size_t{0}, [](std::size_t count, const BasicDraw &draw)
            {
                return count + (draw.instanceModels && !draw.instanceModels->empty()
                                    ? draw.instanceModels->size() : 1u);
            });
        m_timingStats.shadowCandidateCount = shadowDraws.size();

        // Visibility is transient. Evicting resources that are merely outside
        // the current camera frustum makes camera rotation synchronously rebuild
        // meshes, texture mip chains, staging buffers, and Vulkan submissions.
        // Retain the scene cache for the renderer lifetime; Shutdown is the
        // explicit ownership boundary used on project/backend changes.

        m_drawCount = draws.size();
        glm::mat4 projection = cameraData.projection;
        // CameraData uses GLM's negative-one-to-one clip depth. Vulkan and
        // OpenGL contexts with clip control use zero-to-one; older OpenGL
        // contexts must retain the original projection convention.
        if (m_device->UsesZeroToOneClipDepth())
        {
            glm::mat4 depthRangeConversion(1.0f);
            depthRangeConversion[2][2] = 0.5f;
            depthRangeConversion[3][2] = 0.5f;
            projection = depthRangeConversion * projection;
        }
        // Keep the camera projection itself immutable. Temporal jitter is a
        // translation in homogeneous clip space, not an intrinsic change to
        // the perspective projection. Retaining this matrix also avoids
        // reconstructing the unjittered form with projection-layout-specific
        // element edits below.
        const glm::mat4 unjitteredProjection = projection;
        if (hasSceneLights)
        {
            effectiveLighting.pointLights.clear();
            effectiveLighting.spotLights.clear();
            for (const auto *light : sceneLights)
            {
                if (!light || light->intensity <= 0 || light->GetRange() <= 0) continue;
                const BasicPointLight local{light->position, light->GetRange(), light->color,
                                            light->intensity, light->castsShadows};
                if (light->type == scene::LightType::Point)
                    effectiveLighting.pointLights.push_back(local);
                else if (light->type == scene::LightType::Spot)
                    effectiveLighting.spotLights.push_back({local, light->direction, light->spotCone});
            }
        }
        if (scene || !effectiveLighting.localIblEnabled) effectiveLighting.iblCaptures = {};
        // Retained HDR faces work independently of the original OpenGL texture.
        std::erase_if(m_iblCaptures, [](const auto &entry) { return entry.second.version.lifetime.expired(); });
        if (scene && effectiveLighting.localIblEnabled)
        {
            std::size_t index = 0;
            for (const auto &capture : scene->GetIblCaptureVolumes())
            {
                if (index == effectiveLighting.iblCaptures.size()) break;
                if (!capture.IsValid()) continue;
                const auto *source = capture.environmentMapTexture;
                const auto pixels = source->GetCubemapPixels();
                const int resolution = source->GetWidth();
                if (resolution <= 0 || source->GetHeight() != resolution ||
                    pixels.size() != static_cast<std::size_t>(resolution) * resolution * 24) continue;
                auto &cached = m_iblCaptures[source];
                if (!cached.atlas || cached.version.identity != source->GetIdentity() ||
                    cached.version.revision != source->GetContentRevision() || cached.version.lifetime.expired())
                {
                    cached.irradiance = {};
                    double totalWeight = 0;
                    const int step = std::max(resolution / 64, 1);
                    for (int face = 0; face < 6; ++face)
                        for (int y = 0; y < resolution; y += step)
                            for (int x = 0; x < resolution; x += step)
                            {
                                const int blockWidth = std::min(step, resolution - x);
                                const int blockHeight = std::min(step, resolution - y);
                                const float u = (x + blockWidth * 0.5f) * 2.0f / resolution - 1.0f;
                                const float v = (y + blockHeight * 0.5f) * 2.0f / resolution - 1.0f;
                                const glm::vec3 directions[] = {{1,-v,-u},{-1,-v,u},{u,1,v},{u,-1,-v},{u,-v,1},{-u,-v,-1}};
                                const auto d = glm::normalize(directions[face]);
                                const float weight = static_cast<float>(blockWidth * blockHeight) / std::pow(1 + u*u + v*v, 1.5f);
                                totalWeight += weight;
                                const auto offset = ((static_cast<std::size_t>(face) * resolution + y + blockHeight / 2) * resolution + x + blockWidth / 2) * 4;
                                const glm::vec3 color = glm::max(glm::vec3(pixels[offset], pixels[offset+1], pixels[offset+2]), glm::vec3(0));
                                const float basis[] = {0.282095f, 0.488603f*d.y, 0.488603f*d.z, 0.488603f*d.x,
                                    1.092548f*d.x*d.y, 1.092548f*d.y*d.z, 0.315392f*(3*d.z*d.z-1),
                                    1.092548f*d.x*d.z, 0.546274f*(d.x*d.x-d.y*d.y)};
                                for (int coefficient = 0; coefficient < 9; ++coefficient)
                                    cached.irradiance[coefficient] += glm::vec4(color * (basis[coefficient] * weight), 0);
                            }
                    for (int coefficient = 0; coefficient < 9; ++coefficient)
                        cached.irradiance[coefficient] *= static_cast<float>(12.566370614359 / totalWeight) *
                            (coefficient == 0 ? 3.14159265f : coefficient < 4 ? 2.0943951f : 0.78539816f);
                    rhi::TextureDescriptor descriptor;
                    unsigned atlasResolution = 1;
                    while (atlasResolution < static_cast<unsigned>(resolution)) atlasResolution *= 2;
                    descriptor.width = atlasResolution;
                    descriptor.height = atlasResolution * 6;
                    descriptor.format = rhi::Format::R32G32B32A32Float;
                    descriptor.debugName = "Local IBL HDR faces";
                    // Stop at one texel per face; higher levels would mix unrelated faces.
                    descriptor.mipLevels = static_cast<unsigned>(std::log2(atlasResolution)) + 1;
                    // Each face must start on a mip-aligned boundary, including authored non-power-of-two captures.
                    std::vector<float> resampled;
                    std::span<const float> atlasPixels = pixels;
                    if (atlasResolution != static_cast<unsigned>(resolution))
                    {
                        resampled.resize(static_cast<std::size_t>(atlasResolution) * atlasResolution * 24);
                        for (unsigned face = 0; face < 6; ++face)
                            for (unsigned y = 0; y < atlasResolution; ++y)
                                for (unsigned x = 0; x < atlasResolution; ++x)
                                {
                                    const float sx = (x + 0.5f) * resolution / atlasResolution - 0.5f;
                                    const float sy = (y + 0.5f) * resolution / atlasResolution - 0.5f;
                                    const int x0 = static_cast<int>(std::floor(sx)), y0 = static_cast<int>(std::floor(sy));
                                    const float tx = sx - x0, ty = sy - y0;
                                    const auto sample = [&](int px, int py, unsigned c) {
                                        return pixels[((static_cast<std::size_t>(face) * resolution + std::clamp(py, 0, resolution-1)) * resolution + std::clamp(px, 0, resolution-1)) * 4 + c];
                                    };
                                    for (unsigned c = 0; c < 4; ++c)
                                        resampled[((static_cast<std::size_t>(face) * atlasResolution + y) * atlasResolution + x) * 4 + c] =
                                            glm::mix(glm::mix(sample(x0,y0,c), sample(x0+1,y0,c), tx),
                                                     glm::mix(sample(x0,y0+1,c), sample(x0+1,y0+1,c), tx), ty);
                                }
                        atlasPixels = resampled;
                    }
                    cached.atlas = rhi::Texture(*m_device, m_device->CreateTexture(descriptor, std::as_bytes(atlasPixels)));
                    cached.version = {source->GetLifetimeToken(), source->GetIdentity(), source->GetContentRevision()};
                }
                unsigned atlasResolution = 1;
                while (atlasResolution < static_cast<unsigned>(resolution)) atlasResolution *= 2;
                effectiveLighting.iblCaptures[index++] = {cached.atlas.Get(), capture.origin, capture.size,
                    capture.intensity, capture.blendDistance, static_cast<float>(atlasResolution), cached.irradiance};
            }
        }
        // Camera-relative lighting consumers (surface cascade selection,
        // volumetric fog, and voxel injection) must use the camera passed to
        // this render call. Do not rely on every frontend duplicating it into
        // BasicLighting correctly.
        effectiveLighting.cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        effectiveLighting.view = cameraData.view;
        // The physical-sky pass is also the source of the directional
        // environment seen by opaque materials. Keeping this transfer here
        // makes every RHI caller (editor, runtime, and tests) use one contract.
        if (const auto sky = std::ranges::find_if(atmosphereEffects, [](const BasicPostProcessEffect &effect)
                                                  { return effect.type == BasicPostProcessEffectType::PhysicalSky; });
            sky != atmosphereEffects.end())
        {
            effectiveLighting.physicalSkyEnabled = true;
            effectiveLighting.physicalSkyExposure = sky->exposure;
            effectiveLighting.physicalSkyParameters = sky->parameters;
        }
        if (effectiveLighting.shadowsEnabled)
        {
            const float shadowDistance = effectiveLighting.shadowDistance > 0.0f
                                             ? std::min(cameraData.farPlane, effectiveLighting.shadowDistance)
                                             : cameraData.farPlane;
            const float casterDistance = effectiveLighting.shadowCasterDistance > 0.0f
                                             ? effectiveLighting.shadowCasterDistance
                                             : shadowDistance;
            effectiveLighting.shadowDistance = shadowDistance;
            effectiveLighting.shadowCasterDistance = casterDistance;
            if (effectiveLighting.shadowMethod == ShadowMethod::Cascaded)
            {
                const std::uint32_t cascadeCount = std::clamp(effectiveLighting.shadowCascadeCount, 1u, 4u);
                const float cameraNear = std::max(cameraData.nearPlane, 0.01f);
                for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                {
                    const float splitFactor = static_cast<float>(cascade + 1u) / static_cast<float>(cascadeCount);
                    const float logarithmic = cameraNear * std::pow(shadowDistance / cameraNear, splitFactor);
                    const float uniform = cameraNear + (shadowDistance - cameraNear) * splitFactor;
                    effectiveLighting.shadowCascadeSplits[cascade] = glm::mix(
                        uniform, logarithmic, std::clamp(effectiveLighting.shadowSplitLambda, 0.0f, 1.0f));
                }
                if (cascadeCount > 1 && effectiveLighting.shadowNearCascadeDistance > cameraNear)
                {
                    const float nearCascadeEnd = std::clamp(
                        effectiveLighting.shadowNearCascadeDistance,
                        cameraNear + 0.01f,
                        shadowDistance - 0.01f * static_cast<float>(cascadeCount - 1));
                    effectiveLighting.shadowCascadeSplits[0] = nearCascadeEnd;
                    // Treat the explicitly-sized near cascade as a fixed first
                    // partition, then redistribute the remaining range. Merely
                    // replacing split zero can leave split one almost coincident
                    // with it for high logarithmic lambdas (for example 8.00 and
                    // 8.08), producing a visibly degenerate shadow projection.
                    for (std::uint32_t cascade = 1; cascade < cascadeCount; ++cascade)
                    {
                        const float factor = static_cast<float>(cascade) /
                                             static_cast<float>(cascadeCount - 1);
                        const float logarithmic = nearCascadeEnd *
                            std::pow(shadowDistance / nearCascadeEnd, factor);
                        const float uniform = nearCascadeEnd +
                            (shadowDistance - nearCascadeEnd) * factor;
                        effectiveLighting.shadowCascadeSplits[cascade] = glm::mix(
                            uniform, logarithmic,
                            std::clamp(effectiveLighting.shadowSplitLambda, 0.0f, 1.0f));
                    }
                }
                for (std::uint32_t cascade = 1; cascade < cascadeCount; ++cascade)
                    effectiveLighting.shadowCascadeSplits[cascade] = std::max(
                        effectiveLighting.shadowCascadeSplits[cascade],
                        effectiveLighting.shadowCascadeSplits[cascade - 1] + 0.01f);
                effectiveLighting.shadowCascadeSplits[cascadeCount - 1] = shadowDistance;

                for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                {
                    const float cascadeNear = cascade == 0 ? cameraNear
                                                           : effectiveLighting.shadowCascadeSplits[cascade - 1];
                    const auto projection = BuildDirectionalShadowProjection(
                        cameraData, effectiveLighting, cascadeNear,
                        effectiveLighting.shadowCascadeSplits[cascade], casterDistance,
                        std::clamp(static_cast<std::uint32_t>(std::lround(
                                       effectiveLighting.shadowResolution * std::pow(
                                                                                std::clamp(effectiveLighting.shadowCascadeResolutionFalloff, 0.25f, 1.0f),
                                                                                static_cast<float>(cascade)))),
                                   256u, 8192u));
                    effectiveLighting.shadowMatrices[cascade] = projection.matrix;
                    effectiveLighting.shadowCascadeMetrics[cascade] = {
                        projection.worldTexelSize, projection.depthRange, 0.0f, 0.0f};
                }
                if (m_device->GetApi() == rhi::GraphicsApi::Vulkan)
                    effectiveLighting.shadowFlipY = true;
                if (!m_device->UsesZeroToOneClipDepth())
                {
                    effectiveLighting.shadowDepthScale = 0.5f;
                    effectiveLighting.shadowDepthBias = 0.5f;
                }
            }
        }
        std::vector<BasicPostProcessEffect> basicEffects(atmosphereEffects.begin(), atmosphereEffects.end());
        if (scene && m_sceneEffectsEnabled)
        {
            auto oceans = CollectRhiOceans(*scene, effectiveLighting, cameraData.tagFilter);
            basicEffects.insert(basicEffects.end(), std::make_move_iterator(oceans.begin()), std::make_move_iterator(oceans.end()));
        }
        for (const auto *effect : postProcessEffects)
        {
            if (!effect || !effect->IsEnabled())
                continue;
            if (auto adapted = AdaptPostProcessEffect(*effect, cameraData.nearPlane, cameraData.farPlane))
            {
                basicEffects.push_back(std::move(*adapted));
            }
        }
        std::erase_if(basicEffects, [&](const auto &effect) { return !m_graphicsQuality.Allows(effect.type); });
        for (auto &effect : basicEffects)
            if (HasInput(InputsFor(effect.type), BasicPostProcessInput::Depth))
            {
                effect.parameters[5].z = cameraData.nearPlane;
                effect.parameters[5].w = cameraData.farPlane;
            }
        std::stable_sort(basicEffects.begin(), basicEffects.end(), [](const auto &lhs, const auto &rhs)
                         { return StageFor(lhs.type) < StageFor(rhs.type); });
        // Raw diagnostic views must not be adapted or tone-mapped as scene color.
        // Doing so allows auto exposure to flatten AO/indirect buffers to grey.
        const auto terminalDiagnostic = std::find_if(basicEffects.begin(), basicEffects.end(), [](const auto &effect)
                                                     { return (effect.type == BasicPostProcessEffectType::SSAO && effect.parameters[1].y > 0.5f) ||
                                                              (effect.type == BasicPostProcessEffectType::SSGI && effect.parameters[1].z > 0.5f) ||
                                                              (effect.type == BasicPostProcessEffectType::VCTGI &&
                                                               (effect.parameters[3].z > 0.5f || effect.parameters[3].x > 0.5f)) ||
                                                              (effect.type == BasicPostProcessEffectType::SceneComposite && effect.quality != 0u); });
        if (terminalDiagnostic != basicEffects.end())
        {
            const bool displayIndirect = terminalDiagnostic->type == BasicPostProcessEffectType::VCTGI &&
                terminalDiagnostic->parameters[3].z > 0.5f && terminalDiagnostic->parameters[3].x == 0.0f;
            basicEffects.erase(terminalDiagnostic + 1, basicEffects.end());
            if (displayIndirect)
            {
                // Indirect lighting is HDR scene radiance, not a normalized
                // diagnostic colour. A fixed display transform makes faint GI
                // visible without auto exposure concealing on/off differences.
                BasicPostProcessEffect display{BasicPostProcessEffectType::ToneMapping};
                display.exposure = 1.0f;
                display.gamma = 2.2f;
                basicEffects.push_back(display);
            }
        }
        const auto taa = std::find_if(basicEffects.begin(), basicEffects.end(), [](const auto &effect)
                                      { return effect.type == BasicPostProcessEffectType::TAA; });
        glm::vec2 jitterPixels(0.0f);
        if (useTemporalUpscaler || taa != basicEffects.end())
        {
            // AMD's temporal sequence length grows with the square of the
            // upscaling ratio (18/23/32/72 samples for the standard modes).
            // Reusing a short sequence at 3x visibly repeats the camera offset.
            const float upscaleRatio = useTemporalUpscaler
                                           ? static_cast<float>(outputSize.width) /
                                                 static_cast<float>(std::max(renderSize.width, 1u))
                                           : std::sqrt(2.0f);
            const std::uint64_t jitterPhaseCount = static_cast<std::uint64_t>(
                std::max(1l, std::lround(8.0f * upscaleRatio * upscaleRatio)));
            const std::uint64_t sample = m_temporalFrameIndex++ % jitterPhaseCount + 1u;
            // FSR/DLSS replace native TAA and require their full subpixel
            // sequence. Native TAA controls must not disable, shrink, or
            // replace that sequence with the four-pixel diagnostic pattern.
            const bool useNativeTaaJitter = !useTemporalUpscaler && taa != basicEffects.end();
            const bool jitterDebug = useNativeTaaJitter && taa->parameters[3].x > 0.5f;
            const float strength = useNativeTaaJitter
                                       ? std::clamp(taa->parameters[1].w, 0.0f, 2.0f)
                                       : 1.0f;
            if (jitterDebug)
            {
                const float direction = (m_temporalFrameIndex & 1u) == 0u ? -1.0f : 1.0f;
                jitterPixels = glm::vec2(direction * 4.0f, -direction * 4.0f);
            }
            else
            {
                jitterPixels = glm::vec2(Halton(sample, 2u) - 0.5f,
                                         Halton(sample, 3u) - 0.5f) * strength;
            }
            const glm::vec2 jitterNdc = jitterPixels *
                                        glm::vec2(2.0f / static_cast<float>(width),
                                                  2.0f / static_cast<float>(height));
            if (taa != basicEffects.end())
                taa->parameters[2] = {jitterNdc * 0.5f, m_previousTemporalJitterNdc * 0.5f};
            // BasicRenderer applies this offset directly to SV_Position. The
            // camera matrix remains unjittered for stable motion vectors and
            // SDK metadata; the former matrix-edit path did not affect the
            // rasterized geometry on the active Vulkan shader path.
            m_previousTemporalJitterNdc = jitterNdc;
        }
        else
        {
            m_temporalFrameIndex = 0;
            m_previousTemporalJitterNdc = glm::vec2(0.0f);
        }
        const glm::mat4 currentUnjitteredViewProjection = unjitteredProjection * cameraData.view;
        rhi::TemporalUpscalerFrame upscalerFrame{};
        if (useTemporalUpscaler)
        {
            const glm::mat4 clipToPrevious = m_upscalerHistoryValid
                ? m_previousUpscalerViewProjection * glm::inverse(currentUnjitteredViewProjection)
                : glm::mat4(1.0f);
            const glm::mat4 inverseView = glm::inverse(cameraData.view);
            const glm::vec3 cameraPosition(inverseView[3]);
            const glm::vec3 cameraRight = glm::normalize(glm::vec3(inverseView[0]));
            const glm::vec3 cameraUp = glm::normalize(glm::vec3(inverseView[1]));
            const glm::vec3 cameraForward = -glm::normalize(glm::vec3(inverseView[2]));
            upscalerFrame.renderSize = renderSize;
            upscalerFrame.outputSize = outputSize;
            upscalerFrame.jitterPixels = {jitterPixels.x, jitterPixels.y};
            // BasicLit writes signed motion in normalized UV units.
            upscalerFrame.motionVectorScale = {1.0f, 1.0f};
            upscalerFrame.cameraViewToClip = RowMajor(unjitteredProjection);
            upscalerFrame.clipToCameraView = RowMajor(glm::inverse(unjitteredProjection));
            upscalerFrame.clipToPreviousClip = RowMajor(clipToPrevious);
            upscalerFrame.previousClipToClip = RowMajor(glm::inverse(clipToPrevious));
            upscalerFrame.cameraPosition = {cameraPosition.x, cameraPosition.y, cameraPosition.z};
            upscalerFrame.cameraRight = {cameraRight.x, cameraRight.y, cameraRight.z};
            upscalerFrame.cameraUp = {cameraUp.x, cameraUp.y, cameraUp.z};
            upscalerFrame.cameraForward = {cameraForward.x, cameraForward.y, cameraForward.z};
            upscalerFrame.orthographicProjection = orthographic;
            upscalerFrame.orthographicViewWidth = orthographic ? 2.0f / std::abs(unjitteredProjection[0][0]) : 0.0f;
            upscalerFrame.orthographicViewHeight = orthographic ? 2.0f / std::abs(unjitteredProjection[1][1]) : 0.0f;
            upscalerFrame.cameraNear = cameraData.nearPlane;
            upscalerFrame.cameraFar = cameraData.farPlane;
            upscalerFrame.cameraVerticalFovRadians =
                2.0f * std::atan(1.0f / std::max(std::abs(unjitteredProjection[1][1]), 0.0001f));
            upscalerFrame.cameraAspectRatio = static_cast<float>(width) / static_cast<float>(height);
            upscalerFrame.contextId = m_upscalerContextId;
            upscalerFrame.frameIndex = m_temporalFrameIndex;
            upscalerFrame.resetHistory = !m_upscalerHistoryValid || resolutionChanged;
            // BasicRenderer generates velocity from unjittered current and
            // previous transforms, so SDK-side jitter cancellation must stay
            // disabled.
            upscalerFrame.motionVectorsJittered = false;
        }
        setupScope.End();
        const auto setupEnd = std::chrono::steady_clock::now();
        core::CpuScope recordingScope("Render recording", core::CpuCategory::Rendering);
        m_timingStats.sceneSetupMs = millisecondsBetween(translationEnd, setupEnd);
        auto &giDraws = preparation.gi.draws;
        if (std::ranges::any_of(basicEffects, [](const auto &effect) { return effect.type == BasicPostProcessEffectType::VCTGI; }))
        {
            // shadowCommands is the frontend's unculled scene list. GI needs
            // its materials and non-shadow-casting surfaces as well.
            const auto sceneCommands = shadowCommands.empty() ? commands : shadowCommands;
            core::CpuScope giScope("GI packet preparation", core::CpuCategory::Rendering);
            const auto start = std::chrono::steady_clock::now();
            SelectVctScene(sceneCommands, preparation.giSource);
            appendDraws(preparation.giSource, preparation.gi, false, true);
            m_timingStats.giPreparationMs = millisecondsBetween(start, std::chrono::steady_clock::now());
        }
        else
            preparation.gi = {};
        std::size_t particleDrawCount = 0;
        const auto acquireParticleDraw = [&]() -> BasicParticleDraw &
        {
            if (particleDrawCount == m_particleDraws.size())
                m_particleDraws.emplace_back();
            auto &draw = m_particleDraws[particleDrawCount++];
            draw.vertices.clear();
            draw.instances.clear();
            draw.parameters = {};
            draw.texture = {};
            return draw;
        };
        if (scene && m_particleEffectsEnabled)
        {
            core::CpuScope particleScope("Particle render preparation", core::CpuCategory::Rendering);
            const auto inverseView = glm::inverse(cameraData.view);
            const auto inverseProjection = glm::inverse(cameraData.projection);
            const auto particleViewProjection = projection * cameraData.view;
            const ParticleVisibility visibility(cameraData.projection * cameraData.view);
            const glm::vec3 right = glm::normalize(glm::vec3(inverseView[0]));
            const glm::vec3 forward = -glm::normalize(glm::vec3(inverseView[2]));
            const auto hash = [](float seed) {
                const float value = std::sin(seed) * 43758.5453123f;
                return value - std::floor(value);
            };
            for (const auto *system : scene->GetParticleSystemComponents())
            {
                if (!system || !system->IsEnabled() || !system->GetOwner() || !system->GetOwner()->IsActiveInHierarchy() ||
                    (cameraData.tagFilter && !cameraData.tagFilter->Accepts(system->GetOwner())))
                    continue;
                if (system->GetParticleCount() == 0 && !system->GetTrailsEnabled())
                    continue;
                // Cull live billboards before sorting, material lookup, and light
                // selection. Scratch capacity survives across emitters and frames.
                auto &sorted = m_sortedParticles;
                sorted.clear();
                for (const auto &particle : system->GetCpuParticles())
                {
                    if (!particle.active || particle.age >= particle.lifetime)
                        continue;
                    const float maximumSize = system->GetSizeOverLifetimeEnabled()
                        ? std::max(std::abs(particle.size), std::abs(system->GetEndSize())) : std::abs(particle.size);
                    if (visibility.IsVisible(particle.position, maximumSize * 0.707107f))
                        sorted.emplace_back(&particle, glm::dot(particle.position - effectiveLighting.cameraPosition, forward));
                }
                if (sorted.empty() && !system->GetTrailsEnabled())
                    continue;
                std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
                auto &draw = acquireParticleDraw();
                auto &parameters = draw.parameters;
                parameters.viewProjection = particleViewProjection;
                parameters.inverseProjection = inverseProjection;
                parameters.view = cameraData.view;
                auto &v = parameters.values;
                v[0] = glm::vec4(1);
                const auto bindMaterial = [&](BasicParticleDraw &packet, const std::string &reference) {
                    if (reference.empty())
                        return;
                    auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(reference);
                    if (!material)
                        return;
                    const auto &config = material->ReadConfig();
                    packet.parameters.values[0] = config.color;
                    packet.texture = uploadTexture(config.albedoTexture, rhi::Format::R8G8B8A8Srgb, m_textureCache->srgb,
                                                   "Particle albedo");
                    packet.parameters.values[1] = {config.emission, packet.texture ? 1.0f : 0.0f};
                };
                bindMaterial(draw, system->GetMaterialAssetReference());
                v[2] = {static_cast<float>(system->GetRenderShape()), static_cast<float>(system->GetRenderMode()),
                        system->GetSoftParticlesEnabled() ? 1.0f : 0.0f, system->GetSoftParticleDistance()};
                v[3] = {system->GetFlipbookColumns(), system->GetFlipbookRows(), system->GetFlipbookFramesPerSecond(),
                        system->GetFlipbookLooping() ? 1.0f : 0.0f};
                v[4] = {system->GetFlipbookRandomStart() ? 1.0f : 0.0f, system->GetSmokeLightingEnabled() ? 1.0f : 0.0f,
                        system->GetSmokeLightingStrength(), system->GetSmokeAmbient()};
                v[5] = {glm::mat3(cameraData.view) * -effectiveLighting.directionalDirection, 0};
                v[6] = {effectiveLighting.directionalColor * effectiveLighting.directionalIntensity, 0};
                v[7] = {system->GetVolumeDensity(), system->GetVolumeNoiseStrength(), system->GetVolumeNoiseFrequency(),
                        system->GetVolumeEdgeSoftness()};
                v[8].x = system->GetVolumeSelfShadow();
                std::vector<const scene::Light *> smokeLights;
                if (system->GetSmokeLightingEnabled())
                    for (const auto *light : sceneLights)
                        if (light && light->type != scene::LightType::Directional && light->GetRange() > 0 && light->intensity > 0)
                            smokeLights.push_back(light);
                const auto emitterPosition = system->GetOwner()->GetWorldPosition();
                const auto lightScore = [&](const scene::Light *light)
                {
                    return light->intensity * LocalLightAttenuation(glm::length(light->position - emitterPosition), light->GetRange());
                };
                std::stable_sort(smokeLights.begin(), smokeLights.end(), [&](const auto *a, const auto *b)
                    { return lightScore(a) > lightScore(b); });
                const auto localCount = std::min<std::size_t>(smokeLights.size(), 4);
                v[8].y = static_cast<float>(localCount);
                for (std::size_t light = 0; light < localCount; ++light)
                {
                    const auto &local = *smokeLights[light];
                    v[9 + light] = {glm::vec3(cameraData.view * glm::vec4(local.position, 1)), local.GetRange()};
                    v[13 + light] = {local.color * local.intensity, static_cast<float>(local.type)};
                    const auto cone = local.spotCone.Cosines();
                    v[17 + light] = {glm::mat3(cameraData.view) * local.direction, cone.x};
                    v[22][light] = cone.y;
                }
                constexpr glm::vec2 corners[] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f},
                                                 {-0.5f, 0.5f},  {0.5f, -0.5f}, {0.5f, 0.5f}};
                draw.instances.reserve(sorted.size());
                for (const auto &entry : sorted)
                {
                    const auto *particle = entry.first;
                    const float age = glm::clamp(particle->age / std::max(particle->lifetime, 0.0001f), 0.0f, 1.0f);
                    const float size = system->GetSizeOverLifetimeEnabled()
                                           ? glm::mix(particle->size, system->GetEndSize(), age)
                                           : particle->size;
                    glm::vec4 color = particle->color;
                    if (system->GetColorOverLifetimeEnabled())
                        color = glm::mix(color, system->GetEndColor(), age);
                    else
                        color.a *= 1.0f - age;
                    if (system->GetFadeInFraction() > 0)
                        color.a *= glm::smoothstep(0.0f, system->GetFadeInFraction(), age);
                    if (system->GetFadeOutFraction() > 0)
                        color.a *= 1.0f - glm::smoothstep(1.0f - system->GetFadeOutFraction(), 1.0f, age);
                    const float angle =
                        glm::radians(system->GetStartRotation() +
                                     (hash(particle->seed + 23) * 2 - 1) * system->GetStartRotationVariation()) +
                        glm::radians(system->GetRotationSpeed()) *
                            (1 + (hash(particle->seed + 24) * 2 - 1) * system->GetRotationSpeedVariation()) *
                            particle->age;
                    draw.instances.push_back({glm::vec4(particle->position, angle), color,
                        {particle->age, particle->lifetime, hash(particle->seed), size}});
                }
                if (system->GetTrailsEnabled())
                {
                    auto &trail = acquireParticleDraw();
                    trail.parameters.viewProjection = particleViewProjection;
                    trail.parameters.inverseProjection = inverseProjection;
                    trail.parameters.view = cameraData.view;
                    trail.parameters.values[0] = glm::vec4(1);
                    trail.parameters.values[21].x = 1;
                    trail.parameters.values[2].x = 1;
                    trail.parameters.values[3] = {1, 1, 0, 0};
                    bindMaterial(trail, system->GetTrailMaterialAssetReference());
                    std::vector<scene::ParticleTrailRenderSegment> segments;
                    system->BuildTrailRenderSegments(segments);
                    for (const auto &segment : segments)
                    {
                        const auto direction = segment.end - segment.start;
                        if (glm::length(direction) <= 0.0001f || segment.width <= 0 ||
                            !visibility.IsVisible((segment.start + segment.end) * 0.5f,
                                (glm::length(direction) + segment.width) * 0.5f))
                            continue;
                        auto side = glm::cross(forward, glm::normalize(direction));
                        side = glm::length(side) > 0.0001f ? glm::normalize(side) : right;
                        side *= segment.width * 0.5f;
                        const glm::vec3 positions[] = {segment.start - side, segment.start + side, segment.end - side,
                                                       segment.end - side,   segment.start + side, segment.end + side};
                        for (int vertex = 0; vertex < 6; ++vertex)
                            trail.vertices.push_back({positions[vertex],
                                                      segment.color,
                                                      corners[vertex] + 0.5f,
                                                      {0, 1, 0, segment.width},
                                                      segment.start});
                    }
                }
            }
        }
        if (const auto ssr = std::ranges::find_if(basicEffects, [](const auto &effect) { return effect.type == BasicPostProcessEffectType::SSR; }); ssr != basicEffects.end())
        {
            m_timingStats.ssrSteps = std::clamp(ssr->quality, 8u, 128u);
            m_timingStats.ssrRefinementSteps = static_cast<unsigned>(std::clamp(ssr->parameters[1].w, 0.0f, 8.0f));
            const bool full = ssr->parameters[4].w > .5f;
            m_timingStats.ssrTraceSize = full ? renderSize : rhi::Extent2D{(renderSize.width + 1) / 2, (renderSize.height + 1) / 2};
        }
        std::vector<BasicDecalDraw> decals;
        if (scene && m_sceneEffectsEnabled && scene == core::Engine::GetInstance().GetScene())
        {
            for (const auto &command : core::Engine::GetInstance().GetRenderer().GetDecalCommands())
            {
                if (!command.material || command.tint.a <= 0 || std::abs(glm::determinant(command.model)) < 0.000001f)
                    continue;
                const auto &config = command.material->ReadConfig();
                BasicDecalDraw draw;
                draw.parameters.inverseModel = glm::inverse(command.model);
                draw.parameters.color = config.color * command.tint;
                draw.parameters.projectorNormal = {glm::normalize(glm::vec3(command.model[2])), command.normalCutoff};
                draw.parameters.material = {config.uvScale, static_cast<float>(config.alphaMode),
                    config.alphaCutoff / std::max(config.color.a, 0.000001f)};
                draw.texture = uploadTexture(config.albedoTexture, rhi::Format::R8G8B8A8Srgb, m_textureCache->srgb, "Decal albedo");
                // An unresolved textured decal must not become a solid square.
                if (config.albedoTexture && !draw.texture)
                    continue;
                decals.push_back(draw);
            }
        }
        m_renderer->Render(projection * cameraData.view, effectiveLighting, draws, basicEffects, shadowDraws, debugView,
                           useTemporalUpscaler ? &upscalerFrame : nullptr,
                           useTemporalUpscaler ? &currentUnjitteredViewProjection : nullptr, submit, giDraws,
                           std::span<const BasicParticleDraw>(m_particleDraws.data(), particleDrawCount),
                           beforeTemporalResolve, linearOutput, sharedClipJitter, decals, recordingMode);
        m_upscalerStatus.active = useTemporalUpscaler && m_renderer->WasTemporalUpscalerEvaluated();
        m_upscalerStatus.nativeInput = m_upscalerStatus.active &&
                                       m_upscalerOptions.quality != rhi::UpscalerQuality::Dlaa &&
                                       renderSize == outputSize;
        if (useTemporalUpscaler && !m_upscalerStatus.active)
        {
            m_upscalerStatus.reason =
                m_device->GetTemporalUpscalerFailureReason(m_upscalerOptions.technology);
            if (m_upscalerStatus.reason.empty())
                m_upscalerStatus.reason = "Temporal upscaler evaluation did not complete";
        }
        if (m_upscalerStatus.active)
        {
            m_previousUpscalerViewProjection = currentUnjitteredViewProjection;
            m_upscalerHistoryValid = true;
        }
        else
        {
            m_upscalerHistoryValid = false;
        }
        // Resolution tracking also owns the native TAA jitter lifecycle. Do
        // not clear it merely because an external temporal upscaler is absent:
        // doing so marks every native-TAA frame as a resize and restarts the
        // Halton sequence at sample one forever.
        m_previousRenderSize = renderSize;
        m_previousOutputSize = outputSize;
        const auto &frameStats = m_renderer->GetFrameStats();
        const auto &rendererTiming = m_renderer->GetTimingStats();
        m_timingStats.beginFrameMs = rendererTiming.beginFrameMs;
        m_timingStats.shadowRecordingMs = rendererTiming.shadowRecordingMs;
        m_timingStats.geometryRecordingMs = rendererTiming.geometryRecordingMs;
        m_timingStats.postProcessRecordingMs = rendererTiming.postProcessRecordingMs;
        m_timingStats.cameraCompositionMs = rendererTiming.cameraCompositionMs;
        m_timingStats.temporalUpscalerMs = rendererTiming.temporalUpscalerMs;
        m_timingStats.submitMs = rendererTiming.submitMs;
        m_timingStats.recordedGeometryDepthDrawCount = frameStats.geometryDepthDraws;
        m_timingStats.geometryColorOutputs = frameStats.geometryColorOutputs;
        m_timingStats.geometryParameterCreates = frameStats.geometryParameterCreates;
        m_timingStats.geometryParameterReuses = frameStats.geometryParameterReuses;
        m_timingStats.recordedGeometryDrawCount = frameStats.geometryDraws;
        m_timingStats.recordedGeometryInstanceCount = frameStats.geometryInstances;
        m_timingStats.glassPanes = frameStats.glassPanes;
        m_timingStats.glassSnapshots = frameStats.glassSnapshots;
        m_timingStats.glassDepthSnapshots = frameStats.glassDepthSnapshots;
        m_timingStats.glassSnapshotReuseHits = frameStats.glassSnapshotReuseHits;
        m_timingStats.glassBoundsCpuMs = frameStats.glassBoundsCpuMs;
        m_timingStats.glassGroupingCpuMs = frameStats.glassGroupingCpuMs;
        m_timingStats.glassDamageCpuMs = frameStats.glassDamageCpuMs;
        m_timingStats.glassCopyRecordingCpuMs = frameStats.glassCopyRecordingCpuMs;
        m_timingStats.glassDrawRecordingCpuMs = frameStats.glassDrawRecordingCpuMs;
        m_timingStats.glassFullFootprints = frameStats.glassFullFootprints;
        m_timingStats.glassSnapshotPixels = frameStats.glassSnapshotPixels;
        m_timingStats.glassBoundsReasons = frameStats.glassBoundsReasons;
        m_timingStats.glassGroupBoundaries = frameStats.glassGroupBoundaries;
        m_timingStats.pointShadowFaceUpdates = frameStats.pointShadowFaceUpdates;
        m_timingStats.pointShadowFaceHits = frameStats.pointShadowFaceHits;
        m_timingStats.pointShadowObjectUploads = frameStats.pointShadowObjectUploads;
        m_timingStats.pointShadowMaterialUploads = frameStats.pointShadowMaterialUploads;
        m_timingStats.pointShadowInvalidations = frameStats.pointShadowInvalidations;
        m_timingStats.pointShadowAtlasUpdates = frameStats.pointShadowAtlasUpdates;
        m_timingStats.pointShadowAtlasCacheHits = frameStats.pointShadowAtlasCacheHits;
        m_timingStats.pointShadowDraws = frameStats.pointShadowDraws;
        m_timingStats.materialPreparations = frameStats.materialPreparations;
        m_timingStats.materialPreparationHits = frameStats.materialPreparationHits;
        m_timingStats.graphSpecializedDraws = frameStats.graphSpecializedDraws;
        m_timingStats.graphInterpretedDraws = frameStats.graphInterpretedDraws;
        m_timingStats.geometryTriangles = frameStats.geometryTriangles;
        m_timingStats.gpuSkinningDispatches = frameStats.gpuSkinningDispatches;
        m_timingStats.gpuSkinningVertices = frameStats.gpuSkinningVertices;
        m_timingStats.gpuSkinningPaletteBytes = frameStats.gpuSkinningPaletteBytes;
        m_timingStats.renderSize = renderSize;
        m_timingStats.outputSize = outputSize;
        m_timingStats.geometryDiagnosticMode = frameStats.geometryDiagnosticMode;
        m_timingStats.directionalShadowSoftness = effectiveLighting.shadowSoftness;
        m_timingStats.recordedShadowDrawCount = frameStats.ShadowDraws();
        m_timingStats.recordedShadowInstanceCount = frameStats.shadowInstances;
        m_timingStats.shadowObjectUploadCount = frameStats.shadowObjectUploads;
        m_timingStats.shadowCascadeUpdateCount = frameStats.shadowCascadeUpdates;
        m_timingStats.shadowCascadeCacheHitCount = frameStats.shadowCascadeCacheHits;
        m_timingStats.shadowCascadeTargetCount = frameStats.shadowCascadeTargets;
        m_timingStats.occlusion = frameStats.occlusion;
        m_timingStats.occlusionActive = frameStats.occlusionActive;
        m_timingStats.occlusionMode = effectiveLighting.occlusionMode;
        m_timingStats.virtualShadows = frameStats.virtualShadows;
        m_timingStats.virtualShadowsActive = frameStats.virtualShadowsActive;
        m_timingStats.directionalShadowStatus = frameStats.directionalShadowStatus;
        m_timingStats.recordedShadowDrawsByCascade = frameStats.shadowDrawsByCascade;
        m_drawCount = frameStats.geometryDraws;
        const auto renderEnd = std::chrono::steady_clock::now();
        m_timingStats.renderRecordingMs = millisecondsBetween(setupEnd, renderEnd);
        m_timingStats.totalMs = millisecondsBetween(totalStart, renderEnd);
        return true;
    }

    rhi::TextureHandle RhiSceneRenderer::GetColorTexture() const noexcept
    {
        return m_renderer ? m_renderer->GetColorTexture() : rhi::TextureHandle{};
    }

    const glm::mat4 &RhiSceneRenderer::GetInverseViewProjection() const noexcept { return m_renderer->GetInverseViewProjection(); }
    const glm::mat4 &RhiSceneRenderer::GetPreviousViewProjection() const noexcept { return m_renderer->GetPreviousViewProjection(); }

    rhi::TextureHandle RhiSceneRenderer::GetDepthTexture() const noexcept { return m_renderer ? m_renderer->GetDepthTexture() : rhi::TextureHandle{}; }
    rhi::TextureHandle RhiSceneRenderer::GetNormalTexture() const noexcept { return m_renderer ? m_renderer->GetNormalTexture() : rhi::TextureHandle{}; }
    rhi::TextureHandle RhiSceneRenderer::GetMaterialTexture() const noexcept { return m_renderer ? m_renderer->GetMaterialTexture() : rhi::TextureHandle{}; }
    rhi::TextureHandle RhiSceneRenderer::GetMotionTexture() const noexcept { return m_renderer ? m_renderer->GetMotionTexture() : rhi::TextureHandle{}; }
    rhi::TextureHandle RhiSceneRenderer::GetCoverageTexture() const noexcept { return m_renderer ? m_renderer->GetCoverageTexture() : rhi::TextureHandle{}; }
}
