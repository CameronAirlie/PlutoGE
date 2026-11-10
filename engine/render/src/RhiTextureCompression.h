#pragma once

#include "PlutoGE/render/TextureCompression.h"
#include "PlutoGE/render/rhi/RenderDevice.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    class Texture;
    class CompressedTextureStore;
    class TextureCompressionQueue;

    // Which of the renderer's texture caches an encoded texture belongs to.
    enum class TextureCacheKind : std::uint8_t
    {
        Srgb,
        Linear,
        Normal,
    };

    // The renderer's side of material texture compression: decides which uploads to compress and in what format,
    // runs encodes in the background (or immediately for deterministic captures), and hands back finished chains
    // with the source revision they were encoded from. It never touches the renderer's caches; RhiSceneRenderer
    // validates each result against the source's current revision before using it.
    class RhiTextureCompression
    {
    public:
        struct Completed
        {
            const Texture *source = nullptr;
            TextureCacheKind kind = TextureCacheKind::Linear;
            std::weak_ptr<const void> lifetime;
            std::uint64_t identity = 0, revision = 0;
            EncodedTexture texture;
        };

        RhiTextureCompression(const rhi::IRenderDevice &device, TextureCompressionSettings settings);
        ~RhiTextureCompression();
        RhiTextureCompression(const RhiTextureCompression &) = delete;
        RhiTextureCompression &operator=(const RhiTextureCompression &) = delete;

        [[nodiscard]] const TextureCompressionSettings &Settings() const noexcept { return m_settings; }

        // The format `source` should be compressed to for `kind`, or nullopt to keep it RGBA8 (disabled, too small,
        // or no format the device and an encoder share).
        [[nodiscard]] std::optional<rhi::Format> SelectFormat(const Texture &source, TextureCacheKind kind,
                                                              std::span<const std::byte> rgba8) const;

        // Whether an encode of `source` would fit the pending budget now (one encode always fits).
        [[nodiscard]] bool HasCapacityFor(const Texture &source) const;
        // Whether an encode of this source revision is queued or running.
        [[nodiscard]] bool IsPending(const Texture &source, TextureCacheKind kind) const;
        // Queues an encode of `rgba8`. True when it is queued or one for this source revision already is; false
        // when the budget is full (the caller keeps RGBA8 and may ask again later).
        bool Request(const Texture &source, TextureCacheKind kind, rhi::Format format, std::vector<std::byte> rgba8);
        // Encodes on the calling thread (through the store), for renderers that must not show interim textures.
        [[nodiscard]] std::optional<EncodedTexture> EncodeNow(const Texture &source, TextureCacheKind kind, rhi::Format format,
                                                              std::span<const std::byte> rgba8) const;
        // Encodes finished since the last call, in completion order.
        [[nodiscard]] std::vector<Completed> TakeCompleted();
        [[nodiscard]] std::size_t Pending() const;

    private:
        struct PendingRequest
        {
            const Texture *source = nullptr;
            TextureCacheKind kind = TextureCacheKind::Linear;
            std::weak_ptr<const void> lifetime;
            std::uint64_t identity = 0, revision = 0;
            std::size_t bytes = 0;
        };
        static TextureSemantic SemanticFor(TextureCacheKind kind);

        TextureCompressionSettings m_settings;
        std::shared_ptr<CompressedTextureStore> m_store;
        std::unique_ptr<TextureCompressionQueue> m_queue;
        // Format support is fixed for a device; asked once per format.
        std::unordered_map<rhi::Format, bool> m_supported;
        std::uint64_t m_nextTicket = 1;
        std::unordered_map<std::uint64_t, PendingRequest> m_requests;
        std::size_t m_pendingBytes = 0;
    };
}
