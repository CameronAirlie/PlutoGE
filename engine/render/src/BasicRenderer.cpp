#include "PlutoGE/render/BasicRenderer.h"

#include <cstddef>
#include <algorithm>
#include <stdexcept>

namespace PlutoGE::render
{
    namespace
    {
        struct alignas(16) BasicMaterialParameters
        {
            glm::vec4 baseColor{1.0f};
            glm::vec2 uvScale{1.0f};
            float metallic = 0.0f;
            float roughness = 1.0f;
            glm::vec3 emission{0.0f};
            float alphaCutoff = 0.5f;
            std::uint32_t alphaMode = 0;
            std::uint32_t hasNormalTexture = 0;
            std::uint32_t hasMetallicTexture = 0;
            std::uint32_t hasRoughnessTexture = 0;
            std::uint32_t metallicChannel = 0;
            std::uint32_t roughnessChannel = 0;
            std::uint32_t flipNormalY = 0;
            std::uint32_t padding = 0;
        };
        static_assert(sizeof(BasicMaterialParameters) == 80);

        struct alignas(16) BasicFrameParameters
        {
            glm::mat4 viewProjection{1.0f};
            glm::vec4 cameraPositionAmbient{0.0f, 0.0f, 0.0f, 0.3f};
            glm::vec4 directionalDirectionIntensity{0.4f, -0.8f, 0.3f, 1.0f};
            glm::vec4 directionalColor{1.0f};
        };
        static_assert(sizeof(BasicFrameParameters) == 112);

        template <typename T>
        std::span<const std::byte> Bytes(const T &value)
        {
            return std::as_bytes(std::span(&value, 1));
        }

        template <typename T>
        std::span<const std::byte> Bytes(std::span<const T> values)
        {
            return std::as_bytes(values);
        }
    }

    bool BasicRenderer::Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders)
    {
        Shutdown();
        if (shaders.vertex.glsl.empty() && shaders.vertex.spirv.empty())
            return false;
        if (shaders.fragment.glsl.empty() && shaders.fragment.spirv.empty())
            return false;

        try
        {
            m_device = &device;
            rhi::GraphicsPipelineDescriptor descriptor;
            descriptor.vertexShader = shaders.vertex;
            descriptor.fragmentShader = shaders.fragment;
            descriptor.vertexLayout = {
                .stride = sizeof(BasicVertex),
                .attributes = {
                    {0, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
                    {1, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, normal))},
                    {2, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))},
                    {3, rhi::Format::R32G32B32A32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, tangent))},
                },
            };
            // The migration renderer accepts existing scene assets whose
            // winding conventions are not yet normalized across importers.
            descriptor.cullMode = rhi::CullMode::None;
            descriptor.debugName = "BasicRenderer opaque pipeline";
            m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
            m_cameraBuffer = rhi::Buffer(device, device.CreateBuffer({sizeof(BasicFrameParameters), rhi::BufferUsage::Uniform, "BasicRenderer frame"}));

            constexpr std::array<std::uint8_t, 16> checker = {
                255, 255, 255, 255, 80, 80, 80, 255,
                80, 80, 80, 255, 255, 255, 255, 255,
            };
            m_fallbackTexture = rhi::Texture(device, device.CreateTexture({2, 2, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::Sampled, "BasicRenderer checker"}, Bytes(std::span(checker))));
            constexpr std::array<std::uint8_t, 4> neutralNormal = {128, 128, 255, 255};
            constexpr std::array<std::uint8_t, 4> neutralData = {255, 255, 255, 255};
            m_fallbackNormalTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "BasicRenderer neutral normal"}, Bytes(std::span(neutralNormal))));
            m_fallbackDataTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "BasicRenderer neutral material data"}, Bytes(std::span(neutralData))));
            m_fallbackSampler = rhi::Sampler(device, device.CreateSampler({true, true, "BasicRenderer sampler"}));
            return true;
        }
        catch (...)
        {
            Shutdown();
            throw;
        }
    }

    void BasicRenderer::Shutdown()
    {
        m_depthTarget.Reset();
        m_colorTarget.Reset();
        m_fallbackSampler.Reset();
        m_fallbackTexture.Reset();
        m_fallbackNormalTexture.Reset();
        m_fallbackDataTexture.Reset();
        m_objectBuffers.clear();
        m_materialBuffers.clear();
        m_cameraBuffer.Reset();
        m_pipeline.Reset();
        m_device = nullptr;
        m_width = 0;
        m_height = 0;
    }

    BasicMesh BasicRenderer::CreateMesh(const BasicMeshData &data)
    {
        if (!m_device || data.vertices.empty() || data.indices.empty())
            throw std::invalid_argument("BasicRenderer mesh data must be non-empty");

        BasicMesh mesh;
        mesh.m_vertexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {data.vertices.size_bytes(), rhi::BufferUsage::Vertex, "BasicRenderer mesh vertices"}, Bytes(data.vertices)));
        mesh.m_indexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {data.indices.size_bytes(), rhi::BufferUsage::Index, "BasicRenderer mesh indices"}, Bytes(data.indices)));
        mesh.m_indexCount = static_cast<std::uint32_t>(data.indices.size());
        return mesh;
    }

    bool BasicRenderer::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device || width == 0 || height == 0)
            return false;
        if (width == m_width && height == m_height && m_colorTarget && m_depthTarget)
            return true;

        rhi::Texture newColor(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::ColorAttachment, "BasicRenderer color"}));
        rhi::Texture newDepth(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment, "BasicRenderer depth"}));
        m_colorTarget = std::move(newColor);
        m_depthTarget = std::move(newDepth);
        m_width = width;
        m_height = height;
        return true;
    }

    void BasicRenderer::Render(const glm::mat4 &viewProjection, std::span<const BasicDraw> draws)
    {
        Render(viewProjection, BasicLighting{}, draws);
    }

    void BasicRenderer::Render(const glm::mat4 &viewProjection, const BasicLighting &lighting, std::span<const BasicDraw> draws)
    {
        if (!m_device || !m_colorTarget || !m_depthTarget)
            throw std::logic_error("BasicRenderer must be initialized and resized before rendering");

        const BasicFrameParameters frameParameters{
            viewProjection,
            glm::vec4(lighting.cameraPosition, lighting.ambientIntensity),
            glm::vec4(glm::normalize(lighting.directionalDirection), lighting.directionalIntensity),
            glm::vec4(lighting.directionalColor, 1.0f),
        };
        m_device->UpdateBuffer(m_cameraBuffer.Get(), 0, Bytes(frameParameters));
        auto &commands = m_device->GetImmediateContext();
        rhi::RenderingInfo renderingInfo;
        renderingInfo.colorAttachment = m_colorTarget.Get();
        renderingInfo.depthAttachment = m_depthTarget.Get();
        renderingInfo.width = m_width;
        renderingInfo.height = m_height;
        renderingInfo.clearColorValue[0] = 0.04f;
        renderingInfo.clearColorValue[1] = 0.06f;
        renderingInfo.clearColorValue[2] = 0.09f;
        commands.BeginRendering(renderingInfo);
        commands.BindPipeline(m_pipeline.Get());
        commands.BindUniformBuffer(0, m_cameraBuffer.Get());
        std::size_t drawIndex = 0;
        for (const auto &draw : draws)
        {
            if (!draw.mesh || !draw.mesh->IsValid())
                continue;
            if (drawIndex == m_objectBuffers.size())
            {
                m_objectBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                    {sizeof(glm::mat4), rhi::BufferUsage::Uniform, "BasicRenderer object draw"}));
            }
            auto &objectBuffer = m_objectBuffers[drawIndex++];
            m_device->UpdateBuffer(objectBuffer.Get(), 0, Bytes(draw.model));
            commands.BindUniformBuffer(16, objectBuffer.Get());
            if (m_materialBuffers.size() < drawIndex)
            {
                m_materialBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                    {sizeof(BasicMaterialParameters), rhi::BufferUsage::Uniform, "BasicRenderer material draw"}));
            }
            const BasicMaterialParameters materialParameters{
                draw.baseColor, draw.uvScale, draw.metallic, draw.roughness,
                draw.emission, draw.alphaCutoff, draw.alphaMode,
                draw.normalTexture ? 1u : 0u,
                draw.metallicTexture ? 1u : 0u,
                draw.roughnessTexture ? 1u : 0u,
                draw.metallicChannel, draw.roughnessChannel,
                draw.flipNormalY ? 1u : 0u};
            auto &materialBuffer = m_materialBuffers[drawIndex - 1];
            m_device->UpdateBuffer(materialBuffer.Get(), 0, Bytes(materialParameters));
            commands.BindUniformBuffer(8, materialBuffer.Get());
            commands.BindTexture(9, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
            commands.BindTexture(10, draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(), m_fallbackSampler.Get());
            commands.BindTexture(11, draw.metallicTexture ? draw.metallicTexture : m_fallbackDataTexture.Get(), m_fallbackSampler.Get());
            commands.BindTexture(12, draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get(), m_fallbackSampler.Get());
            commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
            commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
            const std::uint32_t availableCount = draw.firstIndex < draw.mesh->m_indexCount
                                                     ? draw.mesh->m_indexCount - draw.firstIndex
                                                     : 0;
            const std::uint32_t requestedCount = draw.indexCount == 0 ? availableCount : draw.indexCount;
            const std::uint32_t drawCount = std::min(requestedCount, availableCount);
            if (drawCount != 0)
                commands.DrawIndexed(drawCount, draw.firstIndex);
        }
        commands.EndRendering();
    }
}
