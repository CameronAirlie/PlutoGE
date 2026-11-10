#pragma once

#include "PlutoGE/platform/ContentDigest.h"
#include "PlutoGE/render/TextureCompression.h"

#include <filesystem>
#include <optional>
#include <span>

namespace PlutoGE::render
{
    // A directory of encoded mip chains addressed by the content they were encoded from, so each texture is encoded
    // once per machine rather than once per run. Entries are immutable; a damaged or mismatched entry reads as a
    // miss and is rewritten. Safe to use from several threads: writes go to a unique temporary file and are renamed.
    class CompressedTextureStore
    {
    public:
        explicit CompressedTextureStore(std::filesystem::path directory);

        // Everything that determines the encoded bytes: the source texels and size, the semantic and the format,
        // and the encoder's identity.
        [[nodiscard]] static content::ContentDigest Key(std::span<const std::byte> rgba8, std::uint32_t width,
                                                        std::uint32_t height, TextureSemantic semantic, rhi::Format format);

        [[nodiscard]] std::optional<EncodedTexture> Load(const content::ContentDigest &key) const;
        bool Save(const content::ContentDigest &key, const EncodedTexture &texture) const;
        [[nodiscard]] const std::filesystem::path &Directory() const noexcept { return m_directory; }

    private:
        [[nodiscard]] std::filesystem::path PathFor(const content::ContentDigest &key) const;
        std::filesystem::path m_directory;
    };
}
