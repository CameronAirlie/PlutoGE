#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RhiPostProcessAdapter.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "rhi/NormalMipmaps.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numeric>

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
                const glm::vec3 horizontal = right * (distance * inverseProjectionX);
                const glm::vec3 vertical = up * (distance * inverseProjectionY);
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

    bool RhiSceneRenderer::Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders)
    {
        Shutdown();
        auto renderer = std::make_unique<BasicRenderer>();
        if (!renderer->Initialize(device, shaders))
            return false;
        m_device = &device;
        m_renderer = std::move(renderer);
        m_upscalerContextId = g_nextUpscalerContextId.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void RhiSceneRenderer::Shutdown()
    {
        if (m_device && m_upscalerContextId != 0)
            m_device->ReleaseTemporalUpscalerContext(m_upscalerContextId);
        m_meshes.clear();
        m_srgbTextures.clear();
        m_linearTextures.clear();
        m_normalTextures.clear();
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
                                  const BasicLighting &lighting, std::span<const RenderCommand> commands,
                                  std::span<const RenderCommand> shadowCommands,
                                  std::span<IPostProcessEffect *const> postProcessEffects,
                                  std::span<const BasicPostProcessEffect> atmosphereEffects,
                                  const TexturePixelReader &texturePixelReader, PostProcessDebugView debugView,
                                  bool submit, const scene::Scene *scene)
    {
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
        const rhi::Extent2D outputSize{width, height};
        const bool temporalUpscalerRequested = m_upscalerOptions.technology != rhi::TemporalUpscaler::None;
        const auto upscalerSupport = temporalUpscalerRequested
                                         ? m_device->GetTemporalUpscalerSupport(m_upscalerOptions.technology)
                                         : rhi::TemporalUpscalerSupport{};
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
        const bool resolutionChanged = renderSize != m_previousRenderSize || outputSize != m_previousOutputSize;
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
        std::vector<BasicDraw> draws;
        draws.reserve(commands.size());
        if (m_normalMipJob.valid() &&
            m_normalMipJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto pixels = m_normalMipJob.get();
            const auto uploadStart = std::chrono::steady_clock::now();
            core::CpuScope uploadScope("Prepared normal texture upload", core::CpuCategory::Rendering);
            ++m_timingStats.textureUploadCount;
            rhi::Texture uploaded(*m_device, m_device->CreateTexture(
                {m_pendingNormalWidth, m_pendingNormalHeight, rhi::Format::R8G8B8A8Unorm,
                 rhi::TextureUsage::Sampled, "Scene normal", false, 1, false, 0, true, true}, pixels));
            m_timingStats.textureUploadMs += millisecondsBetween(uploadStart, std::chrono::steady_clock::now());
            if (uploaded)
                m_normalTextures.emplace(m_pendingNormalSource, std::move(uploaded));
            m_pendingNormalSource = nullptr;
        }
        const auto uploadTexture = [&](const Texture *source, rhi::Format format,
                                       auto &cache,
                                       const char *debugName, bool normalMap = false) -> rhi::TextureHandle
        {
            if (!source || source->GetWidth() <= 0 || source->GetHeight() <= 0 || !texturePixelReader)
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

        const auto appendDraws = [&](std::span<const RenderCommand> sourceCommands,
                                     std::vector<BasicDraw> &destination,
                                     bool shadowOnly, bool giOnly = false)
        {
            for (const auto &command : sourceCommands)
            {
                if (!command.mesh || (shadowOnly && !command.castsShadow))
                    continue;
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
                        vertices.push_back({vertex.position, vertex.normal, vertex.uv, vertex.tangent});
                    mesh = m_meshes.emplace(command.mesh, m_renderer->CreateMesh({vertices, source.indices})).first;
                    m_timingStats.meshUploadMs += millisecondsBetween(meshStart, std::chrono::steady_clock::now());
                }

                std::uint32_t firstIndex = 0;
                std::uint32_t indexCount = 0;
                if (command.submeshIndex < command.mesh->GetSubmeshCount())
                {
                    // Small emissive submeshes must not disappear from the GI
                    // source when the camera selects simplified geometry.
                    const bool emissiveGi = giOnly && command.material &&
                        glm::any(glm::greaterThan(command.material->GetConfig().emission, glm::vec3(0.0f)));
                    const auto range = command.mesh->GetSubmeshLodRange(command.submeshIndex, emissiveGi ? 0u : command.lodIndex);
                    firstIndex = range.indexOffset;
                    indexCount = range.indexCount;
                }
                BasicDraw draw{.mesh = &mesh->second, .model = command.model, .castsShadow = command.castsShadow, .shadowBoundsCenter = command.worldBounds.center, .shadowBoundsRadius = command.worldBounds.radius, .firstIndex = firstIndex, .indexCount = indexCount};
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
                draw.contributesToGi = !command.jointMatrices || command.jointMatrices->empty();
                if (command.material)
                {
                    const auto &material = command.material->GetConfig();
                    if (shadowOnly)
                    {
                        if (!material.castsShadow || material.surfaceType == MaterialSurfaceType::Glass || material.alphaMode == AlphaMode::Blend)
                            continue;
                    }
                    else
                    {
                        draw.baseColor = material.color;
                        draw.uvScale = material.uvScale;
                        draw.metallic = material.metallic;
                        draw.roughness = material.roughness;
                        draw.emission = material.emission;
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
                        const bool transparent = material.surfaceType == MaterialSurfaceType::Glass || material.alphaMode == AlphaMode::Blend;
                        draw.contributesToGi = draw.contributesToGi && !transparent;
                        draw.castsShadow = draw.castsShadow && !transparent;
                        draw.alphaCutoff = material.alphaCutoff;
                        draw.alphaMode = static_cast<std::uint32_t>(material.alphaMode);
                        draw.metallicChannel = static_cast<std::uint32_t>(material.metallicTextureChannel);
                        draw.roughnessChannel = static_cast<std::uint32_t>(material.roughnessTextureChannel);
                        draw.flipNormalY = material.flipNormalY;
                        draw.castsShadow = draw.castsShadow && material.castsShadow;
                        draw.baseColorTexture = uploadTexture(material.albedoTexture, rhi::Format::R8G8B8A8Srgb,
                                                              m_srgbTextures, "Scene albedo");
                        if (!giOnly) draw.normalTexture = uploadTexture(material.normalTexture, rhi::Format::R8G8B8A8Unorm,
                                                           m_normalTextures, "Scene normal", true);
                        draw.metallicTexture = uploadTexture(material.metallicTexture, rhi::Format::R8G8B8A8Unorm,
                                                             m_linearTextures, "Scene metallic");
                        if (!giOnly) draw.roughnessTexture = uploadTexture(material.roughnessTexture, rhi::Format::R8G8B8A8Unorm,
                                                              m_linearTextures, "Scene roughness");
                    }
                }
                draw.previousModel = command.previousModel;
                draw.instanceModels = command.instanceModels;
                draw.previousInstanceModels = command.previousInstanceModels;
                destination.push_back(std::move(draw));
            }
        };
        appendDraws(commands, draws, false);
        std::vector<BasicDraw> shadowDraws;
        if (lighting.shadowsEnabled)
        {
            shadowDraws.reserve(shadowCommands.size());
            appendDraws(shadowCommands, shadowDraws, true);
        }
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
        BasicLighting effectiveLighting = lighting;
        if (scene)
        {
            effectiveLighting.pointLights.clear();
            effectiveLighting.spotLights.clear();
            for (const auto *light : scene->GetLights())
            {
                if (!light || light->intensity <= 0 || light->GetRange() <= 0) continue;
                const BasicPointLight local{light->position, light->GetRange(), light->color,
                                            light->intensity, light->castsShadows};
                if (light->type == scene::LightType::Point)
                    effectiveLighting.pointLights.push_back(local);
                else if (light->type == scene::LightType::Spot)
                    effectiveLighting.spotLights.push_back({local, light->direction});
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
            if (!m_renderer->UsesVirtualShadows(effectiveLighting, draws, shadowDraws))
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
        for (const auto *effect : postProcessEffects)
        {
            if (!effect || !effect->IsEnabled())
                continue;
            if (auto adapted = AdaptPostProcessEffect(*effect))
            {
                if (adapted->type == BasicPostProcessEffectType::DepthOfField)
                    adapted->parameters[2] = {cameraData.nearPlane, cameraData.farPlane, 0.0f, 0.0f};
                if (HasInput(InputsFor(adapted->type), BasicPostProcessInput::Depth))
                {
                    adapted->parameters[5].x = cameraData.nearPlane;
                    adapted->parameters[5].y = cameraData.farPlane;
                }
                basicEffects.push_back(std::move(*adapted));
            }
        }
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
            basicEffects.erase(terminalDiagnostic + 1, basicEffects.end());
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
        std::vector<BasicDraw> giDraws;
        if (std::ranges::any_of(basicEffects, [](const auto &effect) { return effect.type == BasicPostProcessEffectType::VCTGI; }))
        {
            // shadowCommands is the frontend's unculled scene list. GI needs
            // its materials and non-shadow-casting surfaces as well.
            const auto sceneCommands = shadowCommands.empty() ? commands : shadowCommands;
            giDraws.reserve(sceneCommands.size());
            appendDraws(sceneCommands, giDraws, false, true);
        }
        std::vector<BasicParticleDraw> particleDraws;
        if (scene)
        {
            const auto inverseView = glm::inverse(cameraData.view);
            const glm::vec3 right = glm::normalize(glm::vec3(inverseView[0]));
            const glm::vec3 up = glm::normalize(glm::vec3(inverseView[1]));
            const glm::vec3 forward = -glm::normalize(glm::vec3(inverseView[2]));
            const auto hash = [](float seed) {
                const float value = std::sin(seed) * 43758.5453123f;
                return value - std::floor(value);
            };
            for (const auto *system : scene->GetParticleSystemComponents())
            {
                if (!system || !system->IsEnabled() || !system->GetOwner() || !system->GetOwner()->IsActive())
                    continue;
                BasicParticleDraw draw;
                auto &parameters = draw.parameters;
                parameters.viewProjection = projection * cameraData.view;
                parameters.inverseProjection = glm::inverse(cameraData.projection);
                parameters.view = cameraData.view;
                auto &v = parameters.values;
                v[0] = glm::vec4(1);
                const auto bindMaterial = [&](BasicParticleDraw &packet, const std::string &reference) {
                    if (reference.empty())
                        return;
                    auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(reference);
                    if (!material)
                        return;
                    const auto &config = material->GetConfig();
                    packet.parameters.values[0] = config.color;
                    packet.texture = uploadTexture(config.albedoTexture, rhi::Format::R8G8B8A8Srgb, m_srgbTextures,
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
                for (const auto *light : scene->GetLights())
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
                    v[17 + light] = {glm::mat3(cameraData.view) * local.direction, 0};
                }
                std::vector<const scene::ParticleCpuData *> sorted;
                for (const auto &particle : system->GetCpuParticles())
                    if (particle.active && particle.age < particle.lifetime)
                        sorted.push_back(&particle);
                std::sort(sorted.begin(), sorted.end(), [&](const auto *a, const auto *b) {
                    return glm::dot(a->position - effectiveLighting.cameraPosition, forward) >
                           glm::dot(b->position - effectiveLighting.cameraPosition, forward);
                });
                constexpr glm::vec2 corners[] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f},
                                                 {-0.5f, 0.5f},  {0.5f, -0.5f}, {0.5f, 0.5f}};
                draw.vertices.reserve(sorted.size() * 6);
                for (const auto *particle : sorted)
                {
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
                    for (const auto corner : corners)
                    {
                        const glm::vec2 rotated{std::cos(angle) * corner.x + std::sin(angle) * corner.y,
                                                -std::sin(angle) * corner.x + std::cos(angle) * corner.y};
                        draw.vertices.push_back({particle->position + (right * rotated.x + up * rotated.y) * size,
                                                 color,
                                                 corner + 0.5f,
                                                 {particle->age, particle->lifetime, hash(particle->seed), size},
                                                 particle->position});
                    }
                }
                if (!draw.vertices.empty())
                    particleDraws.push_back(std::move(draw));
                if (system->GetTrailsEnabled())
                {
                    BasicParticleDraw trail;
                    trail.parameters.viewProjection = projection * cameraData.view;
                    trail.parameters.inverseProjection = glm::inverse(cameraData.projection);
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
                        if (glm::length(direction) <= 0.0001f || segment.width <= 0)
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
                    if (!trail.vertices.empty())
                        particleDraws.push_back(std::move(trail));
                }
            }
        }
        m_renderer->Render(projection * cameraData.view, effectiveLighting, draws, basicEffects, shadowDraws, debugView,
                           useTemporalUpscaler ? &upscalerFrame : nullptr,
                           useTemporalUpscaler ? &currentUnjitteredViewProjection : nullptr, submit, giDraws,
                           particleDraws);
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
        m_timingStats.temporalUpscalerMs = rendererTiming.temporalUpscalerMs;
        m_timingStats.submitMs = rendererTiming.submitMs;
        m_timingStats.recordedGeometryDrawCount = frameStats.geometryDraws;
        m_timingStats.recordedGeometryInstanceCount = frameStats.geometryInstances;
        m_timingStats.recordedShadowDrawCount = frameStats.ShadowDraws();
        m_timingStats.recordedShadowInstanceCount = frameStats.shadowInstances;
        m_timingStats.shadowObjectUploadCount = frameStats.shadowObjectUploads;
        m_timingStats.shadowCascadeUpdateCount = frameStats.shadowCascadeUpdates;
        m_timingStats.shadowCascadeCacheHitCount = frameStats.shadowCascadeCacheHits;
        m_timingStats.shadowCascadeTargetCount = frameStats.shadowCascadeTargets;
        m_timingStats.virtualShadows = frameStats.virtualShadows;
        m_timingStats.virtualShadowsActive = frameStats.virtualShadowsActive;
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

    rhi::TextureHandle RhiSceneRenderer::GetDepthTexture() const noexcept { return m_renderer ? m_renderer->GetDepthTexture() : rhi::TextureHandle{}; }
    rhi::TextureHandle RhiSceneRenderer::GetNormalTexture() const noexcept { return m_renderer ? m_renderer->GetNormalTexture() : rhi::TextureHandle{}; }
    rhi::TextureHandle RhiSceneRenderer::GetMaterialTexture() const noexcept { return m_renderer ? m_renderer->GetMaterialTexture() : rhi::TextureHandle{}; }
    rhi::TextureHandle RhiSceneRenderer::GetMotionTexture() const noexcept { return m_renderer ? m_renderer->GetMotionTexture() : rhi::TextureHandle{}; }
}
