#include "PlutoGE/render/TextureCooking.h"

#include "CompressedTextureStore.h"
#include "PlutoGE/render/rhi/FormatInfo.h"

namespace PlutoGE::render
{
    SampledFormatQuery DesktopBlockCompressionSupport()
    {
        return [](rhi::Format format) { return rhi::IsBlockCompressed(format); };
    }

    TextureCookOutcome CookCompressedTexture(const std::filesystem::path &directory, std::span<const std::byte> rgba8,
                                             std::uint32_t width, std::uint32_t height, TextureSemantic semantic, bool srgb,
                                             const TextureCompressionSettings &settings, const SampledFormatQuery &targetSupports)
    {
        const auto format = SelectTextureCompression(rgba8, width, height, semantic, srgb, settings, targetSupports);
        if (!format)
            return TextureCookOutcome::Uncompressed;
        // The same key a renderer computes for this texture (TextureCompressionQueue::Run).
        const CompressedTextureStore store(directory);
        const auto key = CompressedTextureStore::Key(rgba8, width, height, semantic, *format);
        if (store.Load(key))
            return TextureCookOutcome::AlreadyCooked;
        const auto encoded = EncodeTexture(rgba8, width, height, semantic, *format);
        return encoded && store.Save(key, *encoded) ? TextureCookOutcome::Encoded : TextureCookOutcome::Failed;
    }
}
