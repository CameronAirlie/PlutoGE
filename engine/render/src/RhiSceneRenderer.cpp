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

        for (const auto &command : commands)
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
                           .firstIndex = firstIndex, .indexCount = indexCount};
            if (command.material)
            {
                const auto &material = command.material->GetConfig();
                draw.baseColor = material.color;
                draw.uvScale = material.uvScale;
                draw.metallic = material.metallic;
                draw.roughness = material.roughness;
                draw.emission = material.emission;
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
                for (const auto &model : *command.instanceModels) { draw.model = model; draws.push_back(draw); }
            else
                draws.push_back(draw);
        }

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
            const glm::vec3 center = effectiveLighting.cameraPosition;
            glm::vec3 direction = effectiveLighting.directionalDirection;
            if (glm::dot(direction, direction) < 0.000001f)
                direction = {0.4f, -0.8f, 0.3f};
            direction = glm::normalize(direction);
            const glm::vec3 up = std::abs(direction.y) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            effectiveLighting.lightViewProjection =
                glm::orthoRH_ZO(-40.0f, 40.0f, -40.0f, 40.0f, 0.1f, 120.0f) *
                glm::lookAtRH(center - direction * 60.0f, center, up);
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
        m_renderer->Render(projection * cameraData.view, effectiveLighting, draws, basicEffects);
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
