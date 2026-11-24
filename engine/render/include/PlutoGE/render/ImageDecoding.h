#pragma once

#include <optional>
#include <string>
#include <vector>

// Image file decoding, independent of textures, renderers and the thread that calls it. Everything that turns an image
// file into texels goes through here: textures loading on the owner thread, decodes started ahead of use on workers
// (TextureDecodeQueue), textures re-reading released pixels (Texture::AcquireRgba8Pixels) and the content cooker
// compressing textures at export (assets/TextureCook.h). One decoder means they all see the same texels, which the
// compressed-texture cache keys on.
namespace PlutoGE::render
{
    // The largest 2D extent both RHI backends accept.
    inline constexpr int kMaxTextureExtent = 16384;

    [[nodiscard]] constexpr bool IsTextureSizeSupported(int width, int height) noexcept
    {
        return width > 0 && height > 0 && width <= kMaxTextureExtent && height <= kMaxTextureExtent;
    }

    // An image's texels as RGBA8 rows (top row first, as stored), and how many channels the file held (1-4).
    struct DecodedImage
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> rgba8;
    };

    // `width` x `height` pixels of `channels` (1-4) bytes each, expanded to RGBA8: grey repeats into RGB, missing
    // alpha is opaque.
    [[nodiscard]] std::vector<unsigned char> ExpandPixelsToRgba8(const unsigned char *pixels, int width, int height, int channels);

    // Reads `path` through the content layer (mounted packs or loose files) and decodes it: the formats stb_image reads
    // (PNG, JPEG, TGA, BMP, ...) and DDS (base level). Nothing for unreadable files, undecodable data and unsupported
    // sizes or channel counts. Safe on any thread.
    [[nodiscard]] std::optional<DecodedImage> DecodeImageFile(const std::string &path);
}
