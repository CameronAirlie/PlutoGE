#pragma once

#include "PlutoGE/render/TextureCompression.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

// Block compression at build time. The renderer compresses material textures on the player's machine the first time it
// samples them (RhiTextureCompression), caching the results by content. Cooking fills a store of the same entries
// while a game is exported, with a source index naming each file's entries (TextureSourceIndex.h), and the runtime searches
// both first (TextureCompressionSettings::cookedRoots), so players neither decode nor encode material textures.
namespace PlutoGE::render
{
    // Where an exported game keeps its cooked store, relative to the project root (inside its content pack).
    inline constexpr std::string_view kCookedTextureDirectory = "Cooked/TextureCompression";

    // The formats a cook targets: desktop GPUs (Direct3D 11 class and later, which every supported one is) sample all of
    // BC1-BC7. A device lacking one simply misses the cooked entry and encodes as it would without cooking.
    [[nodiscard]] SampledFormatQuery DesktopBlockCompressionSupport();

    enum class TextureCookOutcome : std::uint8_t
    {
        Encoded,       // encoded and stored
        AlreadyCooked, // the store already held it
        Uncompressed,  // the renderer keeps such a texture RGBA8 (small, or no format), so there is nothing to cook
        Failed,        // encoding or writing failed
    };

    // The image file a cooked texture was decoded from: the cook records it in the store's source index, so the game
    // loads the texture from its cooked chains without decoding it (TextureSourceIndex.h).
    struct TextureCookSource
    {
        std::filesystem::path file;
        std::filesystem::path contentRoot; // the game's project root: the index names files relative to it
        int channels = 0;                  // as decoded (DecodedImage::channels)
    };

    // Encodes `rgba8` (width x height, as ImageDecoding.h decodes it) the way a renderer sampling it as `semantic` would
    // request it, into the store at `directory`, and records it in the store's source index when `source` names its
    // file. Safe to call from several threads for different files.
    TextureCookOutcome CookCompressedTexture(const std::filesystem::path &directory, std::span<const std::byte> rgba8,
                                             std::uint32_t width, std::uint32_t height, TextureSemantic semantic, bool srgb,
                                             const TextureCompressionSettings &settings = {},
                                             const SampledFormatQuery &targetSupports = DesktopBlockCompressionSupport(),
                                             const TextureCookSource *source = nullptr);
}
