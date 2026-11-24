#pragma once

#include "PlutoGE/platform/ContentDigest.h"
#include "PlutoGE/render/TextureCompression.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // A directory of encoded mip chains addressed by the content they were encoded from, so each texture is encoded
    // once per machine rather than once per run. Entries are immutable; a damaged or mismatched entry reads as a
    // miss and is rewritten. Safe to use from several threads: writes go to a unique temporary file and are renamed.
    //
    // Read-only layers are searched first, through the content layer (mounted content packs included): stores a game's
    // export wrote (TextureCooking.h). Only `directory` is ever written.
    class CompressedTextureStore
    {
    public:
        explicit CompressedTextureStore(std::filesystem::path directory, std::vector<std::filesystem::path> readOnlyLayers = {});

        // Everything that determines the encoded bytes: the source texels and size, the semantic and the format,
        // and the encoder's identity.
        [[nodiscard]] static content::ContentDigest Key(std::span<const std::byte> rgba8, std::uint32_t width,
                                                        std::uint32_t height, TextureSemantic semantic, rhi::Format format);

        [[nodiscard]] std::optional<EncodedTexture> Load(const content::ContentDigest &key) const;
        bool Save(const content::ContentDigest &key, const EncodedTexture &texture) const;
        [[nodiscard]] const std::filesystem::path &Directory() const noexcept { return m_directory; }

        // Where an entry lives below a store's directory.
        [[nodiscard]] static std::filesystem::path PathFor(const std::filesystem::path &directory, const content::ContentDigest &key);
        // An entry's bytes, or nothing when they are not a complete, valid entry.
        [[nodiscard]] static std::optional<EncodedTexture> Parse(std::span<const std::byte> bytes);

    private:
        std::filesystem::path m_directory;
        std::vector<std::filesystem::path> m_readOnlyLayers;
    };
}
