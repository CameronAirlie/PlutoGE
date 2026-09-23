#include "PlutoGE/render/RmlUiRhiRenderer.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <cmath>

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
        Rml::Rectanglef rectangle = Rml::Rectanglef::MakeInvalid();
    };

    struct RmlUiRhiRenderer::Texture
    {
        rhi::Texture resource;
        std::shared_ptr<ExternalTexture> external;
    };

    struct alignas(16) RmlUiRhiRenderer::Parameters
    {
        Rml::Matrix4f transform;
        float translation[2]{};
        float clipYSign = 1.0f;
        float maskMode = 0.0f;
        float inverseSize[2]{};
        float flipTextureY = 0;
        float padding = 0;
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
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
            {8, 1, 0, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {9, 1, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
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
        descriptor.blend.enabled = false;
        descriptor.debugName = "RmlUi clip mask";
        m_clipPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
        m_sampler = rhi::Sampler(device, device.CreateSampler({true, false, "RmlUi sampler", false}));

        const std::array<std::byte, 4> white{
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        m_whiteTexture = std::make_unique<Texture>();
        m_whiteTexture->resource = rhi::Texture(device, device.CreateTexture(
            {1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "RmlUi white", true, 1, false, 1},
            white));
        const std::array<RhiVertex, 6> quad{{
            {{0,0}, {1,1,1,1}, {0,1}}, {{1,0}, {1,1,1,1}, {1,1}},
            {{1,1}, {1,1,1,1}, {1,0}}, {{0,0}, {1,1,1,1}, {0,1}},
            {{1,1}, {1,1,1,1}, {1,0}}, {{0,1}, {1,1,1,1}, {0,0}}}};
        m_compositeVertices = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(quad), rhi::BufferUsage::Vertex, "RmlUi composite vertices"}, Bytes(quad)));
        SetTransform(nullptr);
    }

    RmlUiRhiRenderer::~RmlUiRhiRenderer() = default;

    void RmlUiRhiRenderer::SetViewport(int width, int height)
    {
        m_width = std::max(width, 1);
        m_height = std::max(height, 1);
        SetTransform(nullptr);
    }

    void RmlUiRhiRenderer::BeginFrame(rhi::TextureHandle target, bool beginSubmission)
    {
        if (!m_device || !target || m_frameActive)
            return;
        auto &commands = m_device->GetImmediateContext();
        if (beginSubmission)
            commands.BeginFrame("Runtime UI");
        m_outputTarget = target;
        m_renderScale = 1;
        // Bound the allocation at very large display sizes. Target reuse avoids
        // allocating UI textures every frame; the device retires resized targets.
        if (m_antialiasingEnabled && m_compositeVertices && m_width <= 4096 && m_height <= 4096)
        {
            if (!m_uiTarget || m_targetWidth != m_width * 2 || m_targetHeight != m_height * 2)
            {
                m_uiTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                    {static_cast<std::uint32_t>(m_width * 2), static_cast<std::uint32_t>(m_height * 2),
                     rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment,
                     "RmlUi antialiasing", true, 1, false, 1}));
                m_targetWidth = m_width * 2;
                m_targetHeight = m_height * 2;
            }
            if (m_uiTarget) m_renderScale = 2;
        }
        rhi::RenderingInfo info;
        info.colorAttachments = {m_renderScale == 2 ? m_uiTarget.Get() : target};
        info.width = static_cast<std::uint32_t>(m_width * m_renderScale);
        info.height = static_cast<std::uint32_t>(m_height * m_renderScale);
        info.clearColor = m_renderScale == 2;
        info.clearColorValue[3] = 0;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.SetViewport({0, 0, static_cast<float>(info.width), static_cast<float>(info.height), 0, 1});
        m_parameterCursor = 0;
        m_frameActive = true;
        ApplyScissor();
    }

    void RmlUiRhiRenderer::EndFrame(bool submit)
    {
        if (!m_device || !m_frameActive)
            return;
        auto &commands = m_device->GetImmediateContext();
        commands.EndRendering();
        if (m_renderScale == 2)
        {
            rhi::RenderingInfo composite;
            composite.colorAttachments = {m_outputTarget};
            composite.width = static_cast<std::uint32_t>(m_width);
            composite.height = static_cast<std::uint32_t>(m_height);
            composite.clearColor = composite.clearDepth = false;
            // Binding performs the attachment-to-sampled layout transition on
            // Vulkan, so it must happen outside the dynamic rendering scope.
            commands.BindPipeline(m_pipeline.Get());
            commands.BindTexture(8, m_uiTarget.Get(), m_sampler.Get());
            commands.BindTexture(9, m_whiteTexture->resource.Get(), m_sampler.Get());
            commands.BeginRendering(composite);
            commands.SetViewport({0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1});
            commands.SetScissor({0, 0, composite.width, composite.height});
            Parameters parameters{Rml::Matrix4f::ProjectOrtho(0, 1, 1, 0, -1, 1), {0,0},
                                  m_device->GetApi() == rhi::GraphicsApi::Vulkan ? -1.0f : 1.0f, 0};
            auto &buffer = AcquireParameterBuffer(parameters);
            commands.BindPipeline(m_pipeline.Get());
            commands.BindVertexBuffer(m_compositeVertices.Get());
            commands.BindUniformBuffer(0, buffer.Get());
            // Bilinear sampling at each output pixel averages exactly 2x2
            // source samples. Both source and blend state are premultiplied,
            // preserving transparent edges without dark fringes or scene blur.
            commands.Draw(6);
            commands.EndRendering();
        }
        if (submit)
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
        // RmlUi emits simple rectangular clipping regions as four corners and
        // two triangles. Keep that metadata for allocation-free scissor clips.
        if (vertices.size() == 4 && indices.size() == 6)
        {
            auto bounds = Rml::Rectanglef::FromPositionSize(vertices[0].position, {0,0});
            for (const auto &vertex : vertices) bounds = bounds.Join(vertex.position);
            bool corners = bounds.Width() > 0 && bounds.Height() > 0;
            unsigned cornerBits = 0;
            for (const auto &vertex : vertices)
            {
                const auto point = vertex.position;
                corners &= (point.x == bounds.Left() || point.x == bounds.Right()) &&
                           (point.y == bounds.Top() || point.y == bounds.Bottom());
                cornerBits |= 1u << ((point.x == bounds.Right() ? 1 : 0) + (point.y == bounds.Bottom() ? 2 : 0));
            }
            if (corners && cornerBits == 15) geometry->rectangle = bounds;
        }
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
        const auto resource = texture->external ? texture->external->resource.Get() : texture->resource.Get();
        if (!resource)
            return;
        Parameters parameters{
            m_transform,
            {translation.x, translation.y},
            m_device->GetApi() == rhi::GraphicsApi::Vulkan ? -1.0f : 1.0f,
            0.0f};
        parameters.maskMode = m_maskMode != 0 ? m_maskMode : (m_clipEnabled && m_clipValid ? 4.0f : 0.0f);
        parameters.flipTextureY = texture->external && texture->external->flipY ? 1.0f : 0.0f;
        parameters.inverseSize[0] = 1.0f / (m_width * m_renderScale);
        parameters.inverseSize[1] = 1.0f / (m_height * m_renderScale);
        auto &parameterBuffer = AcquireParameterBuffer(parameters);
        auto &commands = m_device->GetImmediateContext();
        commands.BindPipeline(m_maskMode != 0 ? m_clipPipeline.Get() : m_pipeline.Get());
        commands.BindVertexBuffer(geometry->vertices.Get());
        commands.BindIndexBuffer(geometry->indices.Get());
        commands.BindUniformBuffer(0, parameterBuffer.Get());
        commands.BindTexture(8, resource, m_sampler.Get());
        commands.BindTexture(9, m_clipValid ? m_clipTargets[m_clipIndex].Get() : m_whiteTexture->resource.Get(), m_sampler.Get());
        commands.DrawIndexed(geometry->indexCount);
    }

    void RmlUiRhiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        delete reinterpret_cast<Geometry *>(geometry);
    }

    Rml::TextureHandle RmlUiRhiRenderer::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source)
    {
        if (const auto found = m_externalTextures.find(source); found != m_externalTextures.end())
        {
            auto texture = std::make_unique<Texture>();
            texture->external = found->second;
            dimensions = {texture->external->width, texture->external->height};
            return reinterpret_cast<Rml::TextureHandle>(texture.release());
        }
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

    void RmlUiRhiRenderer::RegisterExternalTexture(const std::string &source, std::shared_ptr<ExternalTexture> texture)
    {
        m_externalTextures[source] = std::move(texture);
    }

    void RmlUiRhiRenderer::UnregisterExternalTexture(const std::string &source)
    {
        if (auto found = m_externalTextures.find(source); found != m_externalTextures.end())
        {
            found->second->resource = {};
            m_externalTextures.erase(found);
            Rml::ReleaseTexture(source, this);
        }
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

    void RmlUiRhiRenderer::EnableClipMask(bool enable)
    {
        m_clipEnabled = enable;
        if (!enable) { m_clipValid = false; m_clipRectangle = Rml::Rectanglei::MakeInvalid(); }
        ApplyScissor();
    }

    void RmlUiRhiRenderer::RenderToClipMask(Rml::ClipMaskOperation operation,
                                            Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation)
    {
        if (!m_frameActive || !geometry) return;
        if (operation != Rml::ClipMaskOperation::Intersect)
        {
            m_clipValid = false;
            m_clipRectangle = Rml::Rectanglei::MakeInvalid();
        }
        const auto &rectangle = reinterpret_cast<Geometry *>(geometry)->rectangle;
        if (rectangle.Valid() && operation != Rml::ClipMaskOperation::SetInverse)
        {
            const Rml::Vector2f corners[] = {rectangle.TopLeft(), rectangle.TopRight(), rectangle.BottomRight(), rectangle.BottomLeft()};
            Rml::Vector2f points[4];
            for (int i = 0; i < 4; ++i)
            {
                const auto p = m_transform * Rml::Vector4f(corners[i].x + translation.x, corners[i].y + translation.y, 0, 1);
                points[i] = {(p.x / p.w + 1) * m_width * 0.5f, (1 - p.y / p.w) * m_height * 0.5f};
            }
            bool axisAligned = true;
            for (int i = 0; i < 4; ++i)
            {
                const auto edge = points[(i + 1) % 4] - points[i];
                axisAligned &= std::abs(edge.x) < 0.001f || std::abs(edge.y) < 0.001f;
            }
            if (axisAligned)
            {
                auto bounds = Rml::Rectanglef::FromPositionSize(points[0], {0,0});
                for (const auto &point : points) bounds = bounds.Join(point);
                auto clip = Rml::Rectanglei::FromCorners(
                    {static_cast<int>(std::floor(bounds.Left())), static_cast<int>(std::floor(bounds.Top()))},
                    {static_cast<int>(std::ceil(bounds.Right())), static_cast<int>(std::ceil(bounds.Bottom()))});
                m_clipRectangle = m_clipRectangle.Valid() ? m_clipRectangle.Intersect(clip) : clip;
                ApplyScissor();
                return;
            }
        }
        auto &commands = m_device->GetImmediateContext();
        commands.EndRendering();
        const int width = m_width * m_renderScale, height = m_height * m_renderScale;
        if (m_clipWidth != width || m_clipHeight != height)
        {
            for (auto &target : m_clipTargets)
                target = rhi::Texture(*m_device, m_device->CreateTexture(
                    {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                     rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment,
                     "RmlUi clip coverage", true, 1, false, 1}));
            m_clipWidth = width; m_clipHeight = height; m_clipValid = false;
        }
        // Ping-pong coverage allows nested intersections, including transformed
        // and rounded clips, without sampling the current render attachment.
        const int destination = 1 - m_clipIndex;
        const bool inverse = operation == Rml::ClipMaskOperation::SetInverse;
        m_maskMode = inverse ? 3.0f : operation == Rml::ClipMaskOperation::Intersect && m_clipValid ? 2.0f : 1.0f;
        commands.BindPipeline(m_clipPipeline.Get());
        commands.BindTexture(9, m_clipValid ? m_clipTargets[m_clipIndex].Get() : m_whiteTexture->resource.Get(), m_sampler.Get());
        rhi::RenderingInfo mask;
        mask.colorAttachments = {m_clipTargets[destination].Get()};
        mask.width = width; mask.height = height; mask.clearColor = true; mask.clearDepth = false;
        for (float &channel : mask.clearColorValue) channel = inverse ? 1.0f : 0.0f;
        commands.BeginRendering(mask);
        commands.SetViewport({0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1});
        commands.SetScissor({0, 0, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
        RenderGeometry(geometry, translation, {});
        commands.EndRendering();
        m_clipIndex = destination; m_clipValid = true; m_maskMode = 0;
        commands.BindPipeline(m_pipeline.Get());
        commands.BindTexture(9, m_clipTargets[m_clipIndex].Get(), m_sampler.Get());
        rhi::RenderingInfo resume;
        resume.colorAttachments = {m_renderScale == 2 ? m_uiTarget.Get() : m_outputTarget};
        resume.width = width; resume.height = height; resume.clearColor = resume.clearDepth = false;
        commands.BeginRendering(resume);
        commands.SetViewport({0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1});
        ApplyScissor();
    }

    void RmlUiRhiRenderer::SetTransform(const Rml::Matrix4f *transform)
    {
        const auto projection = Rml::Matrix4f::ProjectOrtho(
            0, static_cast<float>(m_width), static_cast<float>(m_height), 0, -10000, 10000);
        m_transform = transform ? projection * *transform : projection;
    }

    rhi::Buffer &RmlUiRhiRenderer::AcquireParameterBuffer(const Parameters &parameters)
    {
        const bool fresh = m_parameterBuffers.size() <= m_parameterCursor;
        while (m_parameterBuffers.size() <= m_parameterCursor)
        {
            m_parameterBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                {sizeof(Parameters), rhi::BufferUsage::Uniform, "RmlUi parameters"}));
            m_parameterValues.emplace_back();
        }
        auto &buffer = m_parameterBuffers[m_parameterCursor];
        auto &previous = m_parameterValues[m_parameterCursor++];
        // These buffers persist across frames. Preserve unchanged GPU data;
        // index reuse is safe because the entire parameter block is compared.
        if (fresh || std::memcmp(&previous, &parameters, sizeof(Parameters)) != 0)
        {
            m_device->UpdateBuffer(buffer.Get(), 0, Bytes(parameters));
            previous = parameters;
        }
        return buffer;
    }

    void RmlUiRhiRenderer::ApplyScissor()
    {
        if (!m_frameActive)
            return;
        rhi::Scissor scissor{0, 0, static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height)};
        int left = 0, top = 0, right = m_width, bottom = m_height;
        const auto intersect = [&](const Rml::Rectanglei &region)
        {
            left = std::clamp(region.Left(), left, right);
            top = std::clamp(region.Top(), top, bottom);
            right = std::clamp(region.Right(), left, right);
            bottom = std::clamp(region.Bottom(), top, bottom);
        };
        if (m_scissorEnabled && m_scissor.Valid()) intersect(m_scissor);
        if (m_clipEnabled && m_clipRectangle.Valid()) intersect(m_clipRectangle);
        // RmlUi is top-origin; the RHI UI target uses bottom-origin geometry.
        scissor = {left, m_height - bottom, static_cast<std::uint32_t>(right - left),
                   static_cast<std::uint32_t>(bottom - top)};
        scissor.x *= m_renderScale;
        scissor.y *= m_renderScale;
        scissor.width *= m_renderScale;
        scissor.height *= m_renderScale;
        m_device->GetImmediateContext().SetScissor(scissor);
    }
}
