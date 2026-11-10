#include "RhiTextureCompression.h"

#include "CompressedTextureStore.h"
#include "TextureCompressionQueue.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/FormatInfo.h"

#include <algorithm>

namespace PlutoGE::render
{
    RhiTextureCompression::RhiTextureCompression(const rhi::IRenderDevice &device, TextureCompressionSettings settings)
        : m_settings(std::move(settings))
    {
        for (const auto format : {rhi::Format::BC1RgbaUnorm, rhi::Format::BC1RgbaSrgb, rhi::Format::BC3RgbaUnorm,
                                  rhi::Format::BC3RgbaSrgb, rhi::Format::BC4RUnorm, rhi::Format::BC5RgUnorm,
                                  rhi::Format::BC7RgbaUnorm, rhi::Format::BC7RgbaSrgb})
            m_supported[format] = device.SupportsSampledFormat(format);
        if (!m_settings.enabled)
            return;
        m_store = std::make_shared<CompressedTextureStore>(m_settings.cacheDirectory);
        m_queue = std::make_unique<TextureCompressionQueue>(m_store, m_settings.workerCount);
    }

    RhiTextureCompression::~RhiTextureCompression() = default;

    TextureSemantic RhiTextureCompression::SemanticFor(TextureCacheKind kind)
    {
        switch (kind)
        {
        case TextureCacheKind::Srgb:
            return TextureSemantic::Color;
        case TextureCacheKind::Normal:
            return TextureSemantic::TangentNormal;
        case TextureCacheKind::Linear:
        default:
            return TextureSemantic::Data;
        }
    }

    std::optional<rhi::Format> RhiTextureCompression::SelectFormat(const Texture &source, TextureCacheKind kind,
                                                                   std::span<const std::byte> rgba8) const
    {
        const auto width = static_cast<std::uint64_t>(std::max(source.GetWidth(), 0));
        const auto height = static_cast<std::uint64_t>(std::max(source.GetHeight(), 0));
        if (!m_settings.enabled || width * height < m_settings.minimumTexels || width < 4 || height < 4 ||
            rgba8.size() != width * height * 4)
            return std::nullopt;
        TextureCompressionRequest request;
        request.semantic = SemanticFor(kind);
        request.srgb = kind == TextureCacheKind::Srgb;
        request.usesAlpha = request.semantic != TextureSemantic::TangentNormal && TextureUsesAlpha(rgba8);
        return SelectCompressedFormat(request, [&](rhi::Format format) {
            const auto found = m_supported.find(format);
            return found != m_supported.end() && found->second;
        });
    }

    bool RhiTextureCompression::HasCapacityFor(const Texture &source) const
    {
        const auto bytes = std::size_t(std::max(source.GetWidth(), 0)) * std::size_t(std::max(source.GetHeight(), 0)) * 4;
        return m_queue && (m_pendingBytes == 0 || m_pendingBytes + bytes <= m_settings.maxPendingBytes);
    }

    bool RhiTextureCompression::IsPending(const Texture &source, TextureCacheKind kind) const
    {
        for (const auto &[ticket, request] : m_requests)
            if (request.source == &source && request.kind == kind && request.revision == source.GetContentRevision() &&
                request.identity == source.GetIdentity() && !request.lifetime.expired())
                return true;
        return false;
    }

    bool RhiTextureCompression::Request(const Texture &source, TextureCacheKind kind, rhi::Format format,
                                        std::vector<std::byte> rgba8)
    {
        if (!m_queue)
            return false;
        if (IsPending(source, kind))
            return true;
        if (!HasCapacityFor(source))
            return false;
        const auto ticket = m_nextTicket++;
        m_requests.emplace(ticket, PendingRequest{&source, kind, source.GetLifetimeToken(), source.GetIdentity(),
                                                  source.GetContentRevision(), rgba8.size()});
        m_pendingBytes += rgba8.size();
        m_queue->Submit({ticket, std::move(rgba8), static_cast<std::uint32_t>(source.GetWidth()),
                         static_cast<std::uint32_t>(source.GetHeight()), SemanticFor(kind), format});
        return true;
    }

    std::optional<EncodedTexture> RhiTextureCompression::EncodeNow(const Texture &source, TextureCacheKind kind,
                                                                   rhi::Format format, std::span<const std::byte> rgba8) const
    {
        if (!m_settings.enabled)
            return std::nullopt;
        TextureCompressionQueue::Job job{0, std::vector<std::byte>(rgba8.begin(), rgba8.end()),
                                         static_cast<std::uint32_t>(source.GetWidth()),
                                         static_cast<std::uint32_t>(source.GetHeight()), SemanticFor(kind), format};
        return TextureCompressionQueue::Run(job, m_store.get()).texture;
    }

    std::vector<RhiTextureCompression::Completed> RhiTextureCompression::TakeCompleted()
    {
        std::vector<Completed> completed;
        if (!m_queue)
            return completed;
        for (auto &result : m_queue->TakeCompleted())
        {
            const auto found = m_requests.find(result.ticket);
            if (found == m_requests.end())
                continue;
            const auto request = std::move(found->second);
            m_requests.erase(found);
            m_pendingBytes -= std::min(m_pendingBytes, request.bytes);
            if (!result.texture)
                continue;
            completed.push_back({request.source, request.kind, request.lifetime, request.identity, request.revision,
                                 std::move(*result.texture)});
        }
        return completed;
    }

    std::size_t RhiTextureCompression::Pending() const { return m_requests.size(); }
}
