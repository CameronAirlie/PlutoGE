#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/ui/render/DrawList.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui
{
    class GlyphAtlas;
}

namespace PlutoGE::render
{
    class Texture;

    // Draws ui::DrawList geometry with the NativeUI shader on either RHI
    // backend. Geometry is retained per list key and re-uploaded only when it
    // changes, so static interfaces cost no uploads after their first frame.
    class UIRhiRenderer final
    {
    public:
        // Maps an asset texture reference to a loaded texture (possibly a
        // RenderTexture). Returning null draws the image untextured.
        using TextureResolver = std::function<Texture *(const std::string &reference)>;

        struct DrawItem
        {
            const ui::DrawList *list = nullptr;
            // Stable identity of the list across frames (for example a canvas entity id).
            std::uint64_t key = 0;
        };

        UIRhiRenderer(rhi::IRenderDevice &device,
                      const rhi::GraphicsPipelineDescriptor::ShaderCode &vertexShader,
                      const rhi::GraphicsPipelineDescriptor::ShaderCode &fragmentShader);
        ~UIRhiRenderer();
        UIRhiRenderer(const UIRhiRenderer &) = delete;
        UIRhiRenderer &operator=(const UIRhiRenderer &) = delete;

        explicit operator bool() const noexcept { return static_cast<bool>(m_pipeline); }
        void SetTextureResolver(TextureResolver resolver) { m_textureResolver = std::move(resolver); }

        // Uploads changed glyph pages, then records `items` in order into
        // `target` (width x height pixels) on `commands`. Lists use target
        // pixel coordinates with a bottom-left origin.
        void Render(rhi::TextureHandle target, std::uint32_t width, std::uint32_t height,
                    rhi::ICommandContext &commands, std::span<const DrawItem> items, ui::GlyphAtlas &atlas);

        // Releases geometry for keys not rendered within the last few frames.
        void CollectUnused(std::uint32_t maxIdleFrames = 3);

    private:
        struct ResolvedTexture
        {
            rhi::TextureHandle handle;
            bool flipY = false;
            bool encodeSrgb = false;
        };
        struct CachedTexture;
        struct CachedGeometry;
        struct Parameters;

        void SyncAtlas(ui::GlyphAtlas &atlas);
        ResolvedTexture Resolve(const ui::TextureKey &key);
        ResolvedTexture ResolveAsset(const std::string &reference);
        CachedGeometry &Upload(const DrawItem &item);
        rhi::Buffer &AcquireParameters(const Parameters &parameters);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Sampler m_sampler;
        rhi::Texture m_white;
        std::vector<rhi::Texture> m_atlasPages;
        std::vector<std::uint64_t> m_atlasRevisions;
        TextureResolver m_textureResolver;
        std::unordered_map<std::string, std::unique_ptr<CachedTexture>> m_textures;
        std::unordered_map<std::uint64_t, std::unique_ptr<CachedGeometry>> m_geometry;
        std::vector<rhi::Buffer> m_parameterBuffers;
        std::vector<std::vector<std::byte>> m_parameterValues;
        std::size_t m_parameterCursor = 0;
        std::uint64_t m_frame = 0;
    };
}
