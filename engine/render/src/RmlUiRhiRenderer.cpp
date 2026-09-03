#include "PlutoGE/render/RmlUiRhiRenderer.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace PlutoGE::render
{
    namespace
    {
        struct RhiVertex
        {
            float position[2];
            float color[4];
            float uv[2];
        };

#pragma pack(push, 1)
        struct TgaHeader
        {
            std::uint8_t idLength;
            std::uint8_t colorMapType;
            std::uint8_t dataType;
            std::uint16_t colorMapOrigin;
            std::uint16_t colorMapLength;
            std::uint8_t colorMapDepth;
            std::uint16_t xOrigin;
            std::uint16_t yOrigin;
            std::uint16_t width;
            std::uint16_t height;
            std::uint8_t bitsPerPixel;
            std::uint8_t imageDescriptor;
        };
#pragma pack(pop)

        template <typename T>
        std::span<const std::byte> Bytes(const T &value)
        {
            return {reinterpret_cast<const std::byte *>(&value), sizeof(T)};
        }
    }

    struct RmlUiRhiRenderer::Geometry
    {
        rhi::Buffer vertices;
        rhi::Buffer indices;
        std::uint32_t indexCount = 0;
    };

    struct RmlUiRhiRenderer::Texture
    {
        rhi::Texture resource;
    };

    struct alignas(16) RmlUiRhiRenderer::Parameters
    {
        Rml::Matrix4f transform;
        float translation[2]{};
        float clipYSign = 1.0f;
        float padding = 0.0f;
    };

    RmlUiRhiRenderer::RmlUiRhiRenderer(
        rhi::IRenderDevice &device,
        const rhi::GraphicsPipelineDescriptor::ShaderCode &vertexShader,
        const rhi::GraphicsPipelineDescriptor::ShaderCode &fragmentShader)
        : m_device(&device)
    {
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = vertexShader;
        descriptor.fragmentShader = fragmentShader;
        descriptor.colorFormat = rhi::Format::R8G8B8A8Unorm;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.resourceBindings = {
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex},
            {8, 1, 0, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.vertexLayout = {
            sizeof(RhiVertex),
            {{0, rhi::Format::R32G32Float, offsetof(RhiVertex, position)},
             {1, rhi::Format::R32G32B32A32Float, offsetof(RhiVertex, color)},
             {2, rhi::Format::R32G32Float, offsetof(RhiVertex, uv)}}};
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.depthTest = false;
        descriptor.depthWrite = false;
        descriptor.blend.enabled = true;
        descriptor.debugName = "RmlUi RHI";
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
        m_sampler = rhi::Sampler(device, device.CreateSampler({true, false, "RmlUi sampler", false}));

        const std::array<std::byte, 4> white{
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        m_whiteTexture = std::make_unique<Texture>();
        m_whiteTexture->resource = rhi::Texture(device, device.CreateTexture(
            {1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "RmlUi white", true, 1, false, 1},
            white));
        SetTransform(nullptr);
    }

    RmlUiRhiRenderer::~RmlUiRhiRenderer() = default;

    void RmlUiRhiRenderer::SetViewport(int width, int height)
    {
        m_width = std::max(width, 1);
        m_height = std::max(height, 1);
        SetTransform(nullptr);
    }

    void RmlUiRhiRenderer::BeginFrame(rhi::TextureHandle target)
    {
        if (!m_device || !target || m_frameActive)
            return;
        auto &commands = m_device->GetImmediateContext();
        commands.BeginFrame("Runtime UI");
        rhi::RenderingInfo info;
        info.colorAttachments = {target};
        info.width = static_cast<std::uint32_t>(m_width);
        info.height = static_cast<std::uint32_t>(m_height);
        info.clearColor = false;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.SetViewport({0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1});
        m_parameterCursor = 0;
        m_frameActive = true;
        ApplyScissor();
    }

    void RmlUiRhiRenderer::EndFrame()
    {
        if (!m_device || !m_frameActive)
            return;
        auto &commands = m_device->GetImmediateContext();
        commands.EndRendering();
        commands.Submit();
        m_frameActive = false;
    }

    Rml::CompiledGeometryHandle RmlUiRhiRenderer::CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
    {
        if (!m_device || vertices.empty() || indices.empty() ||
            indices.size() > std::numeric_limits<std::uint32_t>::max())
            return {};
        std::vector<RhiVertex> convertedVertices;
        convertedVertices.reserve(vertices.size());
        for (const auto &vertex : vertices)
        {
            constexpr float scale = 1.0f / 255.0f;
            convertedVertices.push_back({
                {vertex.position.x, vertex.position.y},
                {vertex.colour.red * scale, vertex.colour.green * scale,
                 vertex.colour.blue * scale, vertex.colour.alpha * scale},
                {vertex.tex_coord.x, vertex.tex_coord.y}});
        }
        std::vector<std::uint32_t> convertedIndices;
        convertedIndices.reserve(indices.size());
        for (const int index : indices)
            convertedIndices.push_back(static_cast<std::uint32_t>(index));
        auto geometry = std::make_unique<Geometry>();
        geometry->vertices = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {convertedVertices.size() * sizeof(RhiVertex), rhi::BufferUsage::Vertex, "RmlUi vertices"},
            {reinterpret_cast<const std::byte *>(convertedVertices.data()), convertedVertices.size() * sizeof(RhiVertex)}));
        geometry->indices = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {convertedIndices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index, "RmlUi indices"},
            {reinterpret_cast<const std::byte *>(convertedIndices.data()), convertedIndices.size() * sizeof(std::uint32_t)}));
        geometry->indexCount = static_cast<std::uint32_t>(convertedIndices.size());
        if (!geometry->vertices || !geometry->indices)
            return {};
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }

    void RmlUiRhiRenderer::RenderGeometry(Rml::CompiledGeometryHandle handle,
                                           Rml::Vector2f translation, Rml::TextureHandle textureHandle)
    {
        if (!m_frameActive || !handle)
            return;
        auto *geometry = reinterpret_cast<Geometry *>(handle);
        auto *texture = textureHandle ? reinterpret_cast<Texture *>(textureHandle) : m_whiteTexture.get();
        if (!texture || !texture->resource)
            return;
        Parameters parameters{
            m_transform,
            {translation.x, translation.y},
            m_device->GetApi() == rhi::GraphicsApi::Vulkan ? -1.0f : 1.0f,
            0.0f};
        auto &parameterBuffer = AcquireParameterBuffer();
        m_device->UpdateBuffer(parameterBuffer.Get(), 0, Bytes(parameters));
        auto &commands = m_device->GetImmediateContext();
        commands.BindPipeline(m_pipeline.Get());
        commands.BindVertexBuffer(geometry->vertices.Get());
        commands.BindIndexBuffer(geometry->indices.Get());
        commands.BindUniformBuffer(0, parameterBuffer.Get());
        commands.BindTexture(8, texture->resource.Get(), m_sampler.Get());
        commands.DrawIndexed(geometry->indexCount);
    }

    void RmlUiRhiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        delete reinterpret_cast<Geometry *>(geometry);
    }

    Rml::TextureHandle RmlUiRhiRenderer::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source)
    {
        auto *files = Rml::GetFileInterface();
        const auto file = files ? files->Open(source) : Rml::FileHandle{};
        if (!file)
            return {};
        files->Seek(file, 0, SEEK_END);
        const std::size_t size = files->Tell(file);
        files->Seek(file, 0, SEEK_SET);
        std::vector<Rml::byte> data(size);
        const std::size_t read = files->Read(data.data(), size, file);
        files->Close(file);
        if (read != size || size <= sizeof(TgaHeader))
            return {};
        TgaHeader header{};
        std::memcpy(&header, data.data(), sizeof(header));
        const int channels = header.bitsPerPixel / 8;
        const std::size_t sourceOffset = sizeof(header) + header.idLength;
        const std::size_t pixelCount = static_cast<std::size_t>(header.width) * header.height;
        if (header.dataType != 2 || channels < 3 || sourceOffset + pixelCount * channels > data.size())
            return {};
        std::vector<Rml::byte> rgba(pixelCount * 4);
        for (std::uint32_t y = 0; y < header.height; ++y)
            for (std::uint32_t x = 0; x < header.width; ++x)
            {
                const std::size_t sourceIndex = sourceOffset + (static_cast<std::size_t>(y) * header.width + x) * channels;
                const std::uint32_t destinationY = (header.imageDescriptor & 32) ? y : header.height - y - 1;
                const std::size_t destination = (static_cast<std::size_t>(destinationY) * header.width + x) * 4;
                const auto alpha = channels == 4 ? data[sourceIndex + 3] : Rml::byte{255};
                rgba[destination + 0] = static_cast<Rml::byte>((data[sourceIndex + 2] * alpha) / 255);
                rgba[destination + 1] = static_cast<Rml::byte>((data[sourceIndex + 1] * alpha) / 255);
                rgba[destination + 2] = static_cast<Rml::byte>((data[sourceIndex + 0] * alpha) / 255);
                rgba[destination + 3] = alpha;
            }
        dimensions = {header.width, header.height};
        return GenerateTexture(rgba, dimensions);
    }

    Rml::TextureHandle RmlUiRhiRenderer::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                          Rml::Vector2i dimensions)
    {
        if (!m_device || dimensions.x <= 0 || dimensions.y <= 0 || source.empty())
            return {};
        auto texture = std::make_unique<Texture>();
        texture->resource = rhi::Texture(*m_device, m_device->CreateTexture(
            {static_cast<std::uint32_t>(dimensions.x), static_cast<std::uint32_t>(dimensions.y),
             rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "RmlUi texture", true, 1, false, 1},
            {reinterpret_cast<const std::byte *>(source.data()), source.size()}));
        return texture->resource ? reinterpret_cast<Rml::TextureHandle>(texture.release()) : Rml::TextureHandle{};
    }

    void RmlUiRhiRenderer::ReleaseTexture(Rml::TextureHandle texture)
    {
        delete reinterpret_cast<Texture *>(texture);
    }

    void RmlUiRhiRenderer::EnableScissorRegion(bool enable)
    {
        m_scissorEnabled = enable;
        ApplyScissor();
    }

    void RmlUiRhiRenderer::SetScissorRegion(Rml::Rectanglei region)
    {
        m_scissor = region;
        ApplyScissor();
    }

    void RmlUiRhiRenderer::SetTransform(const Rml::Matrix4f *transform)
    {
        const auto projection = Rml::Matrix4f::ProjectOrtho(
            0, static_cast<float>(m_width), static_cast<float>(m_height), 0, -10000, 10000);
        m_transform = transform ? projection * *transform : projection;
    }

    rhi::Buffer &RmlUiRhiRenderer::AcquireParameterBuffer()
    {
        while (m_parameterBuffers.size() <= m_parameterCursor)
            m_parameterBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                {sizeof(Parameters), rhi::BufferUsage::Uniform, "RmlUi parameters"}));
        return m_parameterBuffers[m_parameterCursor++];
    }

    void RmlUiRhiRenderer::ApplyScissor()
    {
        if (!m_frameActive)
            return;
        rhi::Scissor scissor{0, 0, static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height)};
        if (m_scissorEnabled && m_scissor.Valid())
        {
            const int left = std::clamp(m_scissor.Left(), 0, m_width);
            const int top = std::clamp(m_scissor.Top(), 0, m_height);
            const int right = std::clamp(m_scissor.Right(), left, m_width);
            const int bottom = std::clamp(m_scissor.Bottom(), top, m_height);
            // The Vulkan clip-space correction above mirrors geometry relative
            // to its negative-height viewport. Both backends therefore need
            // RmlUi's top-origin rectangle converted from its bottom edge.
            scissor = {left, m_height - bottom, static_cast<std::uint32_t>(right - left),
                       static_cast<std::uint32_t>(bottom - top)};
        }
        m_device->GetImmediateContext().SetScissor(scissor);
    }
}
