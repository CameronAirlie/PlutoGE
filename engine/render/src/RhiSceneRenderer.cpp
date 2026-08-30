#include "PlutoGE/render/RhiSceneRenderer.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <unordered_set>

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
                                  const TexturePixelReader &texturePixelReader)
    {
        if (!m_renderer || !m_device || width == 0 || height == 0 || !m_renderer->Resize(width, height))
            return false;

        m_sceneCommandCount = commands.size();
        std::vector<BasicDraw> draws;
        draws.reserve(commands.size());
        std::unordered_set<const Mesh *> activeMeshes;
        std::unordered_set<const Texture *> activeSrgbTextures;
        std::unordered_set<const Texture *> activeLinearTextures;

        const auto uploadTexture = [&](const Texture *source, rhi::Format format,
                                       auto &cache, auto &activeTextures,
                                       const char *debugName) -> rhi::TextureHandle
        {
            if (!source || source->GetWidth() <= 0 || source->GetHeight() <= 0 || !texturePixelReader)
                return {};
            activeTextures.insert(source);
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
            activeMeshes.insert(command.mesh);
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
                                                      m_srgbTextures, activeSrgbTextures, "Scene albedo");
                draw.normalTexture = uploadTexture(material.normalTexture, rhi::Format::R8G8B8A8Unorm,
                                                   m_linearTextures, activeLinearTextures, "Scene normal");
                draw.metallicTexture = uploadTexture(material.metallicTexture, rhi::Format::R8G8B8A8Unorm,
                                                     m_linearTextures, activeLinearTextures, "Scene metallic");
                draw.roughnessTexture = uploadTexture(material.roughnessTexture, rhi::Format::R8G8B8A8Unorm,
                                                      m_linearTextures, activeLinearTextures, "Scene roughness");
            }
            if (command.instanceModels && !command.instanceModels->empty())
                for (const auto &model : *command.instanceModels) { draw.model = model; draws.push_back(draw); }
            else
                draws.push_back(draw);
        }

        std::erase_if(m_meshes, [&](const auto &entry) { return !activeMeshes.contains(entry.first); });
        std::erase_if(m_srgbTextures, [&](const auto &entry) { return !activeSrgbTextures.contains(entry.first); });
        std::erase_if(m_linearTextures, [&](const auto &entry) { return !activeLinearTextures.contains(entry.first); });

        m_drawCount = draws.size();
        glm::mat4 projection = cameraData.projection;
        if (m_device->GetApi() == rhi::GraphicsApi::Vulkan)
        {
            glm::mat4 depthRangeConversion(1.0f);
            depthRangeConversion[2][2] = 0.5f;
            depthRangeConversion[3][2] = 0.5f;
            projection = depthRangeConversion * projection;
        }
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
            if (m_device->GetApi() == rhi::GraphicsApi::OpenGL)
            {
                effectiveLighting.shadowDepthScale = 0.5f;
                effectiveLighting.shadowDepthBias = 0.5f;
            }
        }
        m_renderer->Render(projection * cameraData.view, effectiveLighting, draws);
        return true;
    }

    rhi::TextureHandle RhiSceneRenderer::GetColorTexture() const noexcept
    {
        return m_renderer ? m_renderer->GetColorTexture() : rhi::TextureHandle{};
    }
}
