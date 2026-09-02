#include "PlutoGE/render/RhiSceneRenderer.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RhiPostProcessAdapter.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>

namespace PlutoGE::render
{
    namespace
    {
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
            const float aspect = projectionY / projectionX;
            // Match the legacy path: narrow/FOV-animated cameras use a stable
            // conservative 90-degree fit, while wider cameras keep their fit.
            const float inverseProjectionY = 1.0f / std::min(projectionY, 1.0f);
            const float inverseProjectionX = inverseProjectionY * aspect;

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
            const float guard = (std::max(lighting.shadowSoftness, 1.0f) + 1.0f) *
                                    (radius * 2.0f / std::max(shadowResolution, 1u)) +
                                0.01f;
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
        return true;
    }

    void RhiSceneRenderer::Shutdown()
    {
        m_meshes.clear();
        m_srgbTextures.clear();
        m_linearTextures.clear();
        if (m_renderer)
            m_renderer->Shutdown();
        m_renderer.reset();
        m_device = nullptr;
        m_sceneCommandCount = 0;
        m_drawCount = 0;
        m_temporalFrameIndex = 0;
        m_previousTemporalJitterNdc = glm::vec2(0.0f);
    }

    bool RhiSceneRenderer::Render(std::uint32_t width, std::uint32_t height,
                                  const CameraData &cameraData, const BasicLighting &lighting,
                                  std::span<const RenderCommand> commands,
                                  std::span<const RenderCommand> shadowCommands,
                                  std::span<IPostProcessEffect *const> postProcessEffects,
                                  std::span<const BasicPostProcessEffect> atmosphereEffects,
                                  const TexturePixelReader &texturePixelReader,
                                  PostProcessDebugView debugView)
    {
        const auto totalStart = std::chrono::steady_clock::now();
        const auto millisecondsBetween = [](const auto start, const auto end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        };
        m_timingStats = {};
        if (!m_renderer || !m_device || width == 0 || height == 0 || !m_renderer->Resize(width, height))
            return false;

        m_sceneCommandCount = commands.size();
        std::vector<BasicDraw> draws;
        draws.reserve(commands.size());
        const auto uploadTexture = [&](const Texture *source, rhi::Format format,
                                       auto &cache,
                                       const char *debugName) -> rhi::TextureHandle
        {
            if (!source || source->GetWidth() <= 0 || source->GetHeight() <= 0 || !texturePixelReader)
                return {};
            if (const auto cached = cache.find(source); cached != cache.end())
                return cached->second.Get();
            auto pixels = texturePixelReader(*source);
            const auto expectedSize = static_cast<std::size_t>(source->GetWidth()) * source->GetHeight() * 4;
            if (pixels.size() != expectedSize)
                return {};
            rhi::Texture uploaded(*m_device, m_device->CreateTexture(
                                                 {static_cast<std::uint32_t>(source->GetWidth()), static_cast<std::uint32_t>(source->GetHeight()),
                                                  format, rhi::TextureUsage::Sampled, debugName},
                                                 pixels));
            return uploaded ? cache.emplace(source, std::move(uploaded)).first->second.Get()
                            : rhi::TextureHandle{};
        };

        const auto appendDraws = [&](std::span<const RenderCommand> sourceCommands,
                                     std::vector<BasicDraw> &destination,
                                     bool shadowOnly)
        {
            for (const auto &command : sourceCommands)
            {
                if (!command.mesh || (shadowOnly && !command.castsShadow))
                    continue;
                auto mesh = m_meshes.find(command.mesh);
                if (mesh == m_meshes.end())
                {
                    const auto &source = command.mesh->GetMeshData();
                    if (source.vertices.empty() || source.indices.empty())
                        continue;
                    std::vector<BasicVertex> vertices;
                    vertices.reserve(source.vertices.size());
                    for (const auto &vertex : source.vertices)
                        vertices.push_back({vertex.position, vertex.normal, vertex.uv, vertex.tangent});
                    mesh = m_meshes.emplace(command.mesh, m_renderer->CreateMesh({vertices, source.indices})).first;
                }

                std::uint32_t firstIndex = 0;
                std::uint32_t indexCount = 0;
                if (command.submeshIndex < command.mesh->GetSubmeshCount())
                {
                    const auto range = command.mesh->GetSubmeshLodRange(command.submeshIndex, command.lodIndex);
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
                        if (!material.castsShadow)
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
                        draw.alphaCutoff = material.alphaCutoff;
                        draw.alphaMode = static_cast<std::uint32_t>(material.alphaMode);
                        draw.metallicChannel = static_cast<std::uint32_t>(material.metallicTextureChannel);
                        draw.roughnessChannel = static_cast<std::uint32_t>(material.roughnessTextureChannel);
                        draw.flipNormalY = material.flipNormalY;
                        draw.castsShadow = draw.castsShadow && material.castsShadow;
                        draw.baseColorTexture = uploadTexture(material.albedoTexture, rhi::Format::R8G8B8A8Srgb,
                                                              m_srgbTextures, "Scene albedo");
                        draw.normalTexture = uploadTexture(material.normalTexture, rhi::Format::R8G8B8A8Unorm,
                                                           m_linearTextures, "Scene normal");
                        draw.metallicTexture = uploadTexture(material.metallicTexture, rhi::Format::R8G8B8A8Unorm,
                                                             m_linearTextures, "Scene metallic");
                        draw.roughnessTexture = uploadTexture(material.roughnessTexture, rhi::Format::R8G8B8A8Unorm,
                                                              m_linearTextures, "Scene roughness");
                    }
                }
                if (command.instanceModels && !command.instanceModels->empty())
                    for (const auto &model : *command.instanceModels)
                    {
                        draw.model = model;
                        destination.push_back(draw);
                    }
                else
                    destination.push_back(draw);
            }
        };
        appendDraws(commands, draws, false);
        std::vector<BasicDraw> shadowDraws;
        if (lighting.shadowsEnabled)
        {
            shadowDraws.reserve(shadowCommands.size());
            appendDraws(shadowCommands, shadowDraws, true);
        }
        const auto translationEnd = std::chrono::steady_clock::now();
        m_timingStats.commandTranslationMs = millisecondsBetween(totalStart, translationEnd);
        m_timingStats.visibleDrawCount = draws.size();
        m_timingStats.shadowDrawCount = shadowDraws.size();

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
        BasicLighting effectiveLighting = lighting;
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
        if (taa != basicEffects.end())
        {
            const std::uint64_t sample = m_temporalFrameIndex++ % 16u + 1u;
            const float strength = std::clamp(taa->parameters[1].w, 0.0f, 2.0f);
            const glm::vec2 jitterPixels{Halton(sample, 2u) - 0.5f, Halton(sample, 3u) - 0.5f};
            const glm::vec2 jitterNdc = jitterPixels * strength *
                                        glm::vec2(2.0f / static_cast<float>(width),
                                                  2.0f / static_cast<float>(height));
            taa->parameters[2] = {jitterNdc * 0.5f, m_previousTemporalJitterNdc * 0.5f};
            projection[2][0] += jitterNdc.x;
            projection[2][1] += jitterNdc.y;
            m_previousTemporalJitterNdc = jitterNdc;
        }
        else
        {
            m_temporalFrameIndex = 0;
            m_previousTemporalJitterNdc = glm::vec2(0.0f);
        }
        const auto setupEnd = std::chrono::steady_clock::now();
        m_timingStats.sceneSetupMs = millisecondsBetween(translationEnd, setupEnd);
        m_renderer->Render(projection * cameraData.view, effectiveLighting, draws, basicEffects, shadowDraws,
                           debugView);
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
