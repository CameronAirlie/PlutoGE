#include "PlutoGE/render/TextureCooking.h"

#include "CompressedTextureStore.h"
#include "PlutoGE/render/TextureSourceIndex.h"
#include "PlutoGE/render/rhi/FormatInfo.h"

namespace PlutoGE::render
{
    SampledFormatQuery DesktopBlockCompressionSupport()
    {
        return [](rhi::Format format) { return rhi::IsBlockCompressed(format); };
    }

    TextureCookOutcome CookCompressedTexture(const std::filesystem::path &directory, std::span<const std::byte> rgba8,
                                             std::uint32_t width, std::uint32_t height, TextureSemantic semantic, bool srgb,
                                             const TextureCompressionSettings &settings, const SampledFormatQuery &targetSupports,
                                             const TextureCookSource *source)
    {
        const auto format = SelectTextureCompression(rgba8, width, height, semantic, srgb, settings, targetSupports);
        if (!format)
            return TextureCookOutcome::Uncompressed;
        // The same key a renderer computes for this texture (TextureCompressionQueue::Run).
        const CompressedTextureStore store(directory);
        const auto key = CompressedTextureStore::Key(rgba8, width, height, semantic, *format);
        auto outcome = TextureCookOutcome::AlreadyCooked;
        if (!store.Load(key))
        {
            const auto encoded = EncodeTexture(rgba8, width, height, semantic, *format);
            if (!encoded || !store.Save(key, *encoded))
                return TextureCookOutcome::Failed;
            outcome = TextureCookOutcome::Encoded;
        }
        // Under the policy of a renderer on a device with `targetSupports`, so its lookup finds this record.
        if (source && !TextureSourceIndex::RecordInLayer({TextureSourceIndex::DirectoryBeside(directory), source->contentRoot},
                                                         source->file, width, height, source->channels, ComputeTextureMeans(rgba8),
                                                         {semantic, srgb, TextureCompressionPolicy(settings, targetSupports), *format, key}))
            return TextureCookOutcome::Failed;
        return outcome;
    }
}
