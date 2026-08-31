#include "PlutoGE/render/RhiSceneRenderer.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RhiPostProcessAdapter.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::render
{
    namespace
    {
        glm::mat4 BuildDirectionalShadowMatrix(const CameraData &camera,
                                               const BasicLighting &lighting,
                                               float cascadeNear,
                                               float shadowDistance,
                                               float casterDistance)
        {
            const glm::mat4 inverseView = glm::inverse(camera.view);
            const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
            const glm::vec3 right = glm::normalize(glm::vec3(inverseView[0]));
            const glm::vec3 up = glm::normalize(glm::vec3(inverseView[1]));
            const glm::vec3 forward = -glm::normalize(glm::vec3(inverseView[2]));
            const float nearDistance = std::max(cascadeNear, camera.nearPlane);
            const float farDistance = std::max(shadowDistance, nearDistance + 0.01f);
            const float inverseProjectionX = 1.0f / std::max(std::abs(camera.projection[0][0]), 0.0001f);
            const float inverseProjectionY = 1.0f / std::max(std::abs(camera.projection[1][1]), 0.0001f);

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
            for (const auto &corner : corners) center += corner;
            center /= static_cast<float>(corners.size());
            glm::vec3 lightDirection = lighting.directionalDirection;
            if (glm::dot(lightDirection, lightDirection) < 0.000001f)
                lightDirection = {0.4f, -0.8f, 0.3f};
            lightDirection = glm::normalize(lightDirection);
            const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.98f
                                          ? glm::vec3(0.0f, 0.0f, 1.0f)
                                          : glm::vec3(0.0f, 1.0f, 0.0f);
            const float lightOffset = std::max(casterDistance, farDistance);
            const glm::mat4 lightView = glm::lookAtRH(center - lightDirection * lightOffset, center, lightUp);

            glm::vec3 minimum(std::numeric_limits<float>::max());
            glm::vec3 maximum(std::numeric_limits<float>::lowest());
            for (const auto &corner : corners)
            {
                const glm::vec3 lightSpaceCorner = glm::vec3(lightView * glm::vec4(corner, 1.0f));
                minimum = glm::min(minimum, lightSpaceCorner);
                maximum = glm::max(maximum, lightSpaceCorner);
            }
            // Include configured off-frustum casters toward the light and leave
            // a small receiver margin to avoid clipping grazing geometry.
            minimum.z -= lightOffset;
            maximum.z += farDistance * 0.05f;
            const glm::vec2 extent = glm::max(glm::vec2(maximum - minimum), glm::vec2(0.01f));
            const glm::vec2 texelSize = extent / static_cast<float>(std::max(lighting.shadowResolution, 1u));
            glm::vec2 lightSpaceCenter = (glm::vec2(minimum) + glm::vec2(maximum)) * 0.5f;
            lightSpaceCenter = glm::round(lightSpaceCenter / texelSize) * texelSize;
            minimum.x = lightSpaceCenter.x - extent.x * 0.5f;
            maximum.x = lightSpaceCenter.x + extent.x * 0.5f;
            minimum.y = lightSpaceCenter.y - extent.y * 0.5f;
            maximum.y = lightSpaceCenter.y + extent.y * 0.5f;
            return glm::orthoRH_ZO(minimum.x, maximum.x, minimum.y, maximum.y,
                                   std::max(-maximum.z, 0.01f), std::max(-minimum.z, 0.02f)) * lightView;
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
    }

    bool RhiSceneRenderer::Render(std::uint32_t width, std::uint32_t height,
                                  const CameraData &cameraData, const BasicLighting &lighting,
                                  std::span<const RenderCommand> commands,
                                  std::span<const RenderCommand> shadowCommands,
                                  std::span<IPostProcessEffect *const> postProcessEffects,
                                  const TexturePixelReader &texturePixelReader)
    {
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
                 format, rhi::TextureUsage::Sampled, debugName}, pixels));
            return uploaded ? cache.emplace(source, std::move(uploaded)).first->second.Get()
                            : rhi::TextureHandle{};
        };

        const auto appendDraws = [&](std::span<const RenderCommand> sourceCommands,
                                     std::vector<BasicDraw> &destination)
        {
          for (const auto &command : sourceCommands)
          {
            if (!command.mesh)
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
            BasicDraw draw{.mesh = &mesh->second, .model = command.model,
                           .castsShadow = command.castsShadow,
                           .shadowBoundsCenter = command.worldBounds.center,
                           .shadowBoundsRadius = command.worldBounds.radius,
                           .firstIndex = firstIndex, .indexCount = indexCount};
            if (command.material)
            {
                const auto &material = command.material->GetConfig();
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
            if (command.instanceModels && !command.instanceModels->empty())
                for (const auto &model : *command.instanceModels) { draw.model = model; destination.push_back(draw); }
            else
                destination.push_back(draw);
          }
        };
        appendDraws(commands, draws);
        std::vector<BasicDraw> shadowDraws;
        shadowDraws.reserve(shadowCommands.size());
        appendDraws(shadowCommands, shadowDraws);

        // Visibility is transient. Evicting resources that are merely outside
        // the current camera frustum makes camera rotation synchronously rebuild
        // meshes, texture mip chains, staging buffers, and Vulkan submissions.
        // Retain the scene cache for the renderer lifetime; Shutdown is the
        // explicit ownership boundary used on project/backend changes.

        m_drawCount = draws.size();
        glm::mat4 projection = cameraData.projection;
        // CameraData uses GLM's negative-one-to-one clip depth. Both RHI
        // backends use zero-to-one: Vulkan natively and OpenGL through
        // glClipControl. Applying this only to Vulkan clipped the reverse-Z
        // OpenGL scene before rasterization and produced a black viewport.
        glm::mat4 depthRangeConversion(1.0f);
        depthRangeConversion[2][2] = 0.5f;
        depthRangeConversion[3][2] = 0.5f;
        projection = depthRangeConversion * projection;
        BasicLighting effectiveLighting = lighting;
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
                effectiveLighting.shadowCascadeSplits[0] = std::min(
                    effectiveLighting.shadowNearCascadeDistance,
                    effectiveLighting.shadowCascadeSplits[1] - 0.01f);
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
                effectiveLighting.shadowMatrices[cascade] = BuildDirectionalShadowMatrix(
                    cameraData, effectiveLighting, cascadeNear,
                    effectiveLighting.shadowCascadeSplits[cascade], casterDistance);
            }
            if (m_device->GetApi() == rhi::GraphicsApi::Vulkan)
                effectiveLighting.shadowFlipY = true;
            else
            {
                effectiveLighting.shadowDepthScale = 0.5f;
                effectiveLighting.shadowDepthBias = 0.5f;
            }
        }
        std::vector<BasicPostProcessEffect> basicEffects;
        for (const auto *effect : postProcessEffects)
        {
            if (!effect || !effect->IsEnabled())
                continue;
            if (auto adapted = AdaptPostProcessEffect(*effect))
                basicEffects.push_back(std::move(*adapted));
        }
        m_renderer->Render(projection * cameraData.view, effectiveLighting, draws, basicEffects, shadowDraws);
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
