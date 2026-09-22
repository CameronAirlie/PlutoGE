#include "PlutoGE/render/ScenePortrait.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_set>
#include <limits>

namespace PlutoGE::render
{
    bool ScenePortrait::Render(rhi::IRenderDevice &device, const scene::Scene &scene, std::uint32_t rootId,
                              std::span<const std::uint32_t> attachments, int width, int height)
    {
        auto *root = scene.FindEntityByID(rootId);
        if (!root || !root->IsActiveInHierarchy() || width < 32 || height < 32 || width > 1024 || height > 1024)
            return false;
        const auto rootWorld = root->GetWorldTransform();
        if (std::abs(glm::determinant(rootWorld)) < 0.000001f) return false;
        const auto inverseRoot = glm::inverse(rootWorld);
        std::vector<RenderCommand> draws;
        std::unordered_set<std::uint32_t> visited;
        const auto collect = [&](const auto &self, scene::Entity *entity) -> void
        {
            if (!entity || !entity->IsActiveInHierarchy() || !visited.insert(entity->GetID()).second) return;
            if (auto *component = entity->GetComponent<scene::MeshComponent>();
                component && component->IsEnabled() && component->IsVisible() && component->GetMesh())
            {
                auto *mesh = component->GetMesh();
                const auto begin = component->GetSubmeshIndex() < 0 ? 0u : unsigned(component->GetSubmeshIndex());
                const auto end = component->GetSubmeshIndex() < 0 ? mesh->GetSubmeshCount()
                    : std::min(mesh->GetSubmeshCount(), size_t(begin + component->GetSubmeshRangeCount()));
                for (size_t i = begin; i < end; ++i)
                {
                    RenderCommand command;
                    command.mesh = mesh;
                    command.material = component->GetMaterialForSubmesh(i);
                    command.submeshIndex = static_cast<std::uint32_t>(i);
                    command.model = inverseRoot * entity->GetWorldTransform() * component->GetMeshOffsetTransform()
                        * component->GetSubmeshOffsetTransform(i);
                    command.previousModel = command.model;
                    command.castsShadow = false;
                    draws.push_back(command);
                }
            }
            for (auto *child : entity->GetChildren()) self(self, child);
        };
        collect(collect, root);
        for (auto id : attachments)
        {
            auto *entity = scene.FindEntityByID(id);
            if (!entity) return false; // Equipment may still be replacing its visual this frame.
            collect(collect, entity);
        }
        if (draws.empty()) return false;

        CameraData camera;
        camera.view = glm::lookAt(glm::vec3(3, 1.5f, -6), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
        for (const auto &draw : draws)
        {
            const auto transform = camera.view * draw.model;
            for (const auto &vertex : draw.mesh->GetMeshData().vertices)
            {
                const auto p = glm::vec3(transform * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1));
                low = glm::min(low, p); high = glm::max(high, p);
            }
        }
        const auto center = (low + high) * 0.5f;
        const float aspect = float(width) / height;
        const float halfHeight = std::max({(high.y - low.y) * 0.5f, (high.x - low.x) * 0.5f / aspect, 0.1f}) * 1.12f;
        camera.nearPlane = 0.01f;
        camera.farPlane = std::max(100.0f, -low.z + 10.0f);
        camera.projection = glm::ortho(center.x - halfHeight * aspect, center.x + halfHeight * aspect,
                                       center.y - halfHeight, center.y + halfHeight, camera.farPlane, camera.nearPlane);
        if (!m_renderer)
        {
            auto shaders = ShaderArtifactLibrary{}.LoadBasicRendererPackage();
            shaders.virtualShadows = {};
            auto renderer = std::make_unique<RhiSceneRenderer>();
            if (!renderer->Initialize(device, shaders)) return false;
            renderer->SetImmediateTextureUploads(true);
            m_renderer = std::move(renderer);
        }
        BasicLighting lighting;
        lighting.cameraPosition = glm::vec3(3, 1.5f, -6);
        lighting.view = camera.view;
        lighting.ambientIntensity = 0.55f;
        lighting.directionalDirection = glm::normalize(glm::vec3(-.4f, -.8f, .6f));
        const std::array effects{BasicPostProcessEffect{BasicPostProcessEffectType::ToneMapping},
                                 BasicPostProcessEffect{BasicPostProcessEffectType::GammaCorrection}};
        const auto pixels = [](const render::Texture &texture)
        {
            const auto rgba = texture.GetRgba8Pixels();
            const auto *first = reinterpret_cast<const std::byte *>(rgba.data());
            return rgba.empty() ? std::vector<std::byte>{} : std::vector<std::byte>(first, first + rgba.size());
        };
        if (!m_renderer->Render(width, height, camera, lighting, draws, {}, {}, effects, pixels)) return false;
        if (!m_copy)
        {
            rhi::GraphicsPipelineDescriptor copy;
            const ShaderArtifactLibrary shaders;
            copy.vertexShader = shaders.Load("PortraitCopy", "vertex");
            copy.fragmentShader = shaders.Load("PortraitCopy", "fragment");
            copy.colorFormat = rhi::Format::R8G8B8A8Unorm;
            copy.depthFormat = rhi::Format::Undefined;
            copy.depthTest = copy.depthWrite = false;
            copy.cullMode = rhi::CullMode::None;
            copy.resourceBindings = {{0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                                     {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                                     {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
            copy.debugName = "Portrait transparent copy";
            m_copy = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(copy));
            m_sampler = rhi::Sampler(device, device.CreateSampler({}));
            // Vulkan's final display output is flipped relative to its geometry depth target.
            const std::array<float, 4> options{device.GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 0, 0, 0};
            m_copyParameters = rhi::Buffer(device, device.CreateBuffer({sizeof(options), rhi::BufferUsage::Uniform, "Portrait orientation"},
                {reinterpret_cast<const std::byte *>(options.data()), sizeof(options)}));
        }
        if (!m_texture->resource || width != m_texture->width || height != m_texture->height)
        {
            m_texture->resource = rhi::Texture(device, device.CreateTexture({std::uint32_t(width), std::uint32_t(height),
                rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "Cached UI portrait", true}));
            m_texture->width = width; m_texture->height = height; m_texture->flipY = true;
        }
        auto &commands = device.GetImmediateContext();
        commands.BeginFrame("Portrait cache");
        rhi::RenderingInfo target;
        target.colorAttachments = {m_texture->resource.Get()}; target.width = width; target.height = height;
        commands.BeginRendering(target);
        commands.BindPipeline(m_copy.Get());
        commands.BindUniformBuffer(0, m_copyParameters.Get());
        commands.BindTexture(1, m_renderer->GetColorTexture(), m_sampler.Get());
        commands.BindTexture(2, m_renderer->GetDepthTexture(), m_sampler.Get());
        commands.Draw(3);
        commands.EndRendering();
        commands.Submit();
        ++m_renderCount;
        return true;
    }
}
