#include "PlutoGE/render/UIRhiRenderer.h"

#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/ui/text/GlyphAtlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace PlutoGE::render
{
    namespace
    {
        template <typename T>
        std::span<const std::byte> Bytes(std::span<const T> values)
        {
            return {reinterpret_cast<const std::byte *>(values.data()), values.size_bytes()};
        }

        template <typename T>
        bool SameContents(const std::vector<T> &a, const std::vector<T> &b)
        {
            return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
        }
    }

    struct UIRhiRenderer::CachedTexture
    {
        Texture *source = nullptr;
        std::weak_ptr<const void> lifetime;
        std::uint64_t identity = 0;
        std::uint64_t revision = 0;
        rhi::Texture texture;
        bool attempted = false; // missing textures are not looked up again
    };

    struct UIRhiRenderer::CachedGeometry
    {
        std::vector<ui::DrawVertex> vertices;
        std::vector<std::uint32_t> indices;
        rhi::Buffer vertexBuffer;
        rhi::Buffer indexBuffer;
        std::uint64_t lastFrame = 0;
    };

    struct alignas(16) UIRhiRenderer::Parameters
    {
        glm::mat4 transform{1.0f};
        float flipTextureY = 0.0f;
        float encodeSrgb = 0.0f;
        float reserved[2]{};
    };

    UIRhiRenderer::UIRhiRenderer(rhi::IRenderDevice &device,
                                 const rhi::GraphicsPipelineDescriptor::ShaderCode &vertexShader,
                                 const rhi::GraphicsPipelineDescriptor::ShaderCode &fragmentShader)
        : m_device(&device)
    {
        using ui::DrawVertex;
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = vertexShader;
        descriptor.fragmentShader = fragmentShader;
        descriptor.colorFormat = rhi::Format::R8G8B8A8Unorm;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.resourceBindings = {
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
            {8, 1, 0, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.vertexLayout = {
            sizeof(DrawVertex),
            {{0, rhi::Format::R32G32Float, offsetof(DrawVertex, position)},
             {1, rhi::Format::R32G32Float, offsetof(DrawVertex, uv)},
             {2, rhi::Format::R32G32B32A32Float, offsetof(DrawVertex, color)},
             {3, rhi::Format::R32G32B32A32Float, offsetof(DrawVertex, local)},
             {4, rhi::Format::R32G32B32A32Float, offsetof(DrawVertex, radii)},
             {5, rhi::Format::R32G32B32A32Float, offsetof(DrawVertex, params)},
             {6, rhi::Format::R32G32B32A32Float, offsetof(DrawVertex, borderColor)},
             {7, rhi::Format::R32G32B32A32Float, offsetof(DrawVertex, clipRect)},
             {8, rhi::Format::R32G32B32A32Float, offsetof(DrawVertex, clipRadii)}}};
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.depthTest = false;
        descriptor.depthWrite = false;
        descriptor.blend.enabled = true; // premultiplied alpha
        descriptor.debugName = "Native UI";
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
        m_sampler = rhi::Sampler(device, device.CreateSampler({.linearFiltering = true,
                                                               .repeat = false,
                                                               .debugName = "Native UI sampler",
                                                               .mipFiltering = true}));
        const std::array<std::byte, 4> white{std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        m_white = rhi::Texture(device, device.CreateTexture(
            {1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Native UI white", true, 1, false, 1}, white));
    }

    UIRhiRenderer::~UIRhiRenderer() = default;

    void UIRhiRenderer::SyncAtlas(ui::GlyphAtlas &atlas)
    {
        const auto &pages = atlas.GetPages();
        m_atlasPages.resize(std::max(m_atlasPages.size(), pages.size()));
        m_atlasRevisions.resize(m_atlasPages.size(), 0);
        for (std::uint32_t index = 0; index < pages.size(); ++index)
        {
            const auto &page = pages[index];
            if (m_atlasPages[index] && m_atlasRevisions[index] == page.revision)
                continue;
            const auto size = static_cast<std::uint32_t>(page.size);
            bool updated = false;
            if (m_atlasPages[index] && page.dirty)
            {
                // Upload only the rows and columns that changed.
                const auto left = static_cast<std::uint32_t>(std::floor(page.dirty->min.x));
                const auto top = static_cast<std::uint32_t>(std::floor(page.dirty->min.y));
                const auto right = std::min(static_cast<std::uint32_t>(std::ceil(page.dirty->max.x)), size);
                const auto bottom = std::min(static_cast<std::uint32_t>(std::ceil(page.dirty->max.y)), size);
                if (right > left && bottom > top)
                {
                    const std::size_t rowBytes = static_cast<std::size_t>(right - left) * 4;
                    std::vector<std::byte> region(rowBytes * (bottom - top));
                    for (std::uint32_t row = top; row < bottom; ++row)
                        std::memcpy(region.data() + (row - top) * rowBytes,
                                    page.rgba.data() + (static_cast<std::size_t>(row) * size + left) * 4, rowBytes);
                    updated = m_device->UpdateTexture(m_atlasPages[index].Get(), {left, top, right - left, bottom - top}, region);
                }
            }
            if (!updated)
                m_atlasPages[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {size, size, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Native UI glyph atlas", true, 1, false, 1},
                    Bytes(std::span(page.rgba))));
            m_atlasRevisions[index] = page.revision;
            atlas.ClearDirty(index);
        }
    }

    UIRhiRenderer::ResolvedTexture UIRhiRenderer::ResolveAsset(const std::string &reference)
    {
        auto &entry = m_textures[reference];
        if (!entry)
            entry = std::make_unique<CachedTexture>();
        if (!entry->attempted || (entry->source && entry->lifetime.expired()))
        {
            entry->attempted = true;
            entry->source = m_textureResolver ? m_textureResolver(reference) : nullptr;
            entry->lifetime = entry->source ? entry->source->GetLifetimeToken() : std::weak_ptr<const void>{};
            entry->texture = {};
            entry->revision = 0;
        }
        Texture *source = entry->source;
        if (!source)
            return {};
        // Render textures are drawn into each frame; sample the live target.
        if (auto *renderTexture = dynamic_cast<RenderTexture *>(source))
            return {renderTexture->GetGpuTexture(*m_device), true, true};

        if (!entry->texture || entry->identity != source->GetIdentity() || entry->revision != source->GetContentRevision())
        {
            const auto pixels = source->GetRgba8Pixels();
            const auto width = static_cast<std::uint32_t>(std::max(source->GetWidth(), 0));
            const auto height = static_cast<std::uint32_t>(std::max(source->GetHeight(), 0));
            entry->texture = {};
            if (width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4)
                entry->texture = rhi::Texture(*m_device, m_device->CreateTexture(
                    {width, height, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Native UI image", true, 1, false, 0},
                    Bytes(pixels)));
            entry->identity = source->GetIdentity();
            entry->revision = source->GetContentRevision();
        }
        return {entry->texture.Get(), false, false};
    }

    UIRhiRenderer::ResolvedTexture UIRhiRenderer::Resolve(const ui::TextureKey &key)
    {
        switch (key.kind)
        {
        case ui::TextureKey::Kind::GlyphAtlas:
            return {key.page < m_atlasPages.size() ? m_atlasPages[key.page].Get() : rhi::TextureHandle{}, false, false};
        case ui::TextureKey::Kind::Asset:
            return ResolveAsset(key.path);
        default:
            return {};
        }
    }

    UIRhiRenderer::CachedGeometry &UIRhiRenderer::Upload(const DrawItem &item)
    {
        auto &entry = m_geometry[item.key];
        if (!entry)
            entry = std::make_unique<CachedGeometry>();
        const auto &vertices = item.list->GetVertices();
        const auto &indices = item.list->GetIndices();
        if (!entry->vertexBuffer || !SameContents(entry->vertices, vertices) || !SameContents(entry->indices, indices))
        {
            entry->vertices = vertices;
            entry->indices = indices;
            // New buffers rather than updates: the previous frame may still be
            // reading the old ones; the device retires them when it is done.
            entry->vertexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {vertices.size() * sizeof(ui::DrawVertex), rhi::BufferUsage::Vertex, "Native UI vertices"},
                Bytes(std::span(vertices))));
            entry->indexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index, "Native UI indices"},
                Bytes(std::span(indices))));
        }
        entry->lastFrame = m_frame;
        return *entry;
    }

    rhi::Buffer &UIRhiRenderer::AcquireParameters(const Parameters &parameters)
    {
        const auto bytes = std::span(reinterpret_cast<const std::byte *>(&parameters), sizeof(Parameters));
        const bool fresh = m_parameterBuffers.size() <= m_parameterCursor;
        if (fresh)
        {
            m_parameterBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                {sizeof(Parameters), rhi::BufferUsage::Uniform, "Native UI parameters"}));
            m_parameterValues.emplace_back();
        }
        auto &buffer = m_parameterBuffers[m_parameterCursor];
        auto &previous = m_parameterValues[m_parameterCursor++];
        // Buffers persist across frames; skip uploads when the block is unchanged.
        if (fresh || previous.size() != bytes.size() || std::memcmp(previous.data(), bytes.data(), bytes.size()) != 0)
        {
            m_device->UpdateBuffer(buffer.Get(), 0, bytes);
            previous.assign(bytes.begin(), bytes.end());
        }
        return buffer;
    }

    void UIRhiRenderer::Render(rhi::TextureHandle target, std::uint32_t width, std::uint32_t height,
                               rhi::ICommandContext &commands, std::span<const DrawItem> items, ui::GlyphAtlas &atlas)
    {
        if (!m_pipeline || !target || width == 0 || height == 0)
            return;
        ++m_frame;
        m_parameterCursor = 0;
        SyncAtlas(atlas);

        // Resolve textures and upload geometry before the rendering scope opens.
        struct Prepared
        {
            const ui::DrawList *list = nullptr;
            CachedGeometry *geometry = nullptr;
            std::vector<ResolvedTexture> textures;
        };
        std::vector<Prepared> prepared;
        for (const auto &item : items)
        {
            if (!item.list || item.list->Empty())
                continue;
            Prepared entry{item.list, &Upload(item), {}};
            for (const auto &key : item.list->GetTextures())
                entry.textures.push_back(Resolve(key));
            prepared.push_back(std::move(entry));
        }
        if (prepared.empty())
            return;

        // Render textures were last written as attachments; binding them here
        // performs the transition outside the rendering scope.
        bool pipelineBound = false;
        for (const auto &entry : prepared)
            for (const auto &texture : entry.textures)
                if (texture.flipY && texture.handle)
                {
                    if (!pipelineBound)
                        commands.BindPipeline(m_pipeline.Get());
                    pipelineBound = true;
                    commands.BindTexture(8, texture.handle, m_sampler.Get());
                }

        rhi::RenderingInfo info;
        info.colorAttachments = {target};
        info.width = width;
        info.height = height;
        info.clearColor = false;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.SetViewport({0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1});
        commands.BindPipeline(m_pipeline.Get());

        // Target pixels (bottom-left origin) to clip space. Vulkan's negative
        // viewport flips y, as for the RmlUi renderer.
        const float ySign = m_device->GetApi() == rhi::GraphicsApi::Vulkan ? -1.0f : 1.0f;
        glm::mat4 transform(1.0f);
        transform[0][0] = 2.0f / static_cast<float>(width);
        transform[1][1] = ySign * 2.0f / static_cast<float>(height);
        transform[3][0] = -1.0f;
        transform[3][1] = -ySign;

        const glm::vec2 limit(static_cast<float>(width), static_cast<float>(height));
        for (const auto &entry : prepared)
        {
            commands.BindVertexBuffer(entry.geometry->vertexBuffer.Get());
            commands.BindIndexBuffer(entry.geometry->indexBuffer.Get());
            for (const auto &command : entry.list->GetCommands())
            {
                if (command.indexCount == 0)
                    continue;
                const glm::vec2 low = glm::clamp(glm::floor(command.scissor.min), glm::vec2(0.0f), limit);
                const glm::vec2 high = glm::clamp(glm::ceil(command.scissor.max), glm::vec2(0.0f), limit);
                if (high.x <= low.x || high.y <= low.y)
                    continue;
                const ResolvedTexture texture = command.texture < entry.textures.size() ? entry.textures[command.texture]
                                                                                         : ResolvedTexture{};
                Parameters parameters;
                parameters.transform = transform;
                parameters.flipTextureY = texture.flipY ? 1.0f : 0.0f;
                parameters.encodeSrgb = texture.encodeSrgb ? 1.0f : 0.0f;
                commands.BindUniformBuffer(0, AcquireParameters(parameters).Get());
                commands.BindTexture(8, texture.handle ? texture.handle : m_white.Get(), m_sampler.Get());
                commands.SetScissor({static_cast<std::int32_t>(low.x), static_cast<std::int32_t>(low.y),
                                     static_cast<std::uint32_t>(high.x - low.x), static_cast<std::uint32_t>(high.y - low.y)});
                commands.DrawIndexed(command.indexCount, command.indexOffset);
            }
        }
        commands.EndRendering();
    }

    void UIRhiRenderer::CollectUnused(std::uint32_t maxIdleFrames)
    {
        std::erase_if(m_geometry, [&](const auto &entry) { return m_frame - entry.second->lastFrame > maxIdleFrames; });
        std::erase_if(m_textures, [](const auto &entry) { return entry.second->source && entry.second->lifetime.expired(); });
    }
}
