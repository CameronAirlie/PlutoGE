#include "PlutoGE/render/BasicRenderer.h"

#include <cstddef>
#include <algorithm>
#include <stdexcept>

namespace PlutoGE::render
{
    namespace
    {
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
                },
            };
            // The migration renderer accepts existing scene assets whose
            // winding conventions are not yet normalized across importers.
            descriptor.cullMode = rhi::CullMode::None;
            descriptor.debugName = "BasicRenderer opaque pipeline";
            m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
            m_cameraBuffer = rhi::Buffer(device, device.CreateBuffer({sizeof(glm::mat4), rhi::BufferUsage::Uniform, "BasicRenderer camera"}));

            constexpr std::array<std::uint8_t, 16> checker = {
                255, 255, 255, 255, 80, 80, 80, 255,
                80, 80, 80, 255, 255, 255, 255, 255,
            };
            m_fallbackTexture = rhi::Texture(device, device.CreateTexture({2, 2, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::Sampled, "BasicRenderer checker"}, Bytes(std::span(checker))));
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
        m_objectBuffers.clear();
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
        if (!m_device || !m_colorTarget || !m_depthTarget)
            throw std::logic_error("BasicRenderer must be initialized and resized before rendering");

        m_device->UpdateBuffer(m_cameraBuffer.Get(), 0, Bytes(viewProjection));
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
        commands.BindTexture(8, m_fallbackTexture.Get(), m_fallbackSampler.Get());

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
