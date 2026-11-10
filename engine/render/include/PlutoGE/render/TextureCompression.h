#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Block compression of material textures (BC1/BC3/BC5, with room for further encoders such as BC7).
//
// The renderer keeps decoded RGBA8 pixels on the CPU and, for textures whose meaning it knows, replaces the RGBA8
// upload with a block-compressed one built here: 4-8x less memory and sampling bandwidth. The pieces are separate so
// each can change on its own:
//  - TextureSemantic says what texels mean; SelectCompressedFormat picks a format for a semantic from the formats
//    that both the device and a registered encoder support.
//  - BuildTextureMipChain builds the full mip chain the way the semantic needs (sRGB-correct colour, plain data,
//    or length-preserving tangent normals).
//  - TextureBlockEncoder encodes 4x4 blocks of one format. RegisterTextureBlockEncoder adds or replaces one, so a
//    better encoder (or a new format) needs no change to the policy or the renderer.
//  - EncodeTexture combines the two into a complete, uploadable mip chain.
namespace PlutoGE::render
{
    enum class TextureSemantic : std::uint8_t
    {
        // Colour in sRGB or linear encoding (albedo, emission, sheen colour). Mips average in linear light when sRGB.
        Color,
        // Non-colour data sampled per channel (metallic, roughness, occlusion, masks).
        Data,
        // Tangent-space normals in RGB. Compressed normals keep X and Y only (BC5); shaders rebuild Z.
        TangentNormal,
    };

    // How shaders must decode a material normal texture.
    enum class NormalTextureEncoding : std::uint8_t
    {
        // RGB holds X, Y and Z.
        Rgb = 0,
        // RG holds X and Y; Z is rebuilt as sqrt(1 - x^2 - y^2).
        Xy = 1,
    };

    [[nodiscard]] constexpr NormalTextureEncoding NormalEncodingFor(rhi::Format format) noexcept
    {
        return format == rhi::Format::BC5RgUnorm ? NormalTextureEncoding::Xy : NormalTextureEncoding::Rgb;
    }

    struct TextureCompressionSettings
    {
        bool enabled = true;
        // Textures with fewer texels stay RGBA8: they cost little memory and many are generated data.
        std::uint32_t minimumTexels = 128 * 128;
        // Encoded mip chains, keyed by content. Empty disables the cache (every texture is encoded each run).
        std::filesystem::path cacheDirectory;
        // Encoding threads for background compression (at least one).
        std::uint32_t workerCount = 2;
        // Source texels held by queued encodes. Textures beyond it keep their RGBA8 upload and are queued when
        // sampled again after earlier encodes finish, so loading a large scene cannot hold every texture twice.
        std::size_t maxPendingBytes = std::size_t{256} << 20;

        // Enabled unless PLUTO_TEXTURE_COMPRESSION=0, caching under the system temporary directory.
        [[nodiscard]] static TextureCompressionSettings FromEnvironment();
    };

    struct TextureCompressionRequest
    {
        TextureSemantic semantic = TextureSemantic::Color;
        bool srgb = false;
        // Any texel's alpha below 255 (see TextureUsesAlpha). Opaque colour and data can use the smaller BC1.
        bool usesAlpha = false;
    };

    // A complete mip chain in `format`, packed from level 0 down as rhi::TextureDescriptor::mipmapsProvided expects.
    struct EncodedTexture
    {
        rhi::Format format = rhi::Format::Undefined;
        std::uint32_t width = 0, height = 0, mipLevels = 0;
        std::vector<std::byte> data;
    };

    // Encodes 4x4 blocks of one block-compressed format.
    class TextureBlockEncoder
    {
    public:
        virtual ~TextureBlockEncoder() = default;
        [[nodiscard]] virtual rhi::Format Format() const noexcept = 0;
        // Identifies the encoder and its settings in cache keys: change it whenever the output changes.
        [[nodiscard]] virtual std::string_view Identity() const noexcept = 0;
        // `texels` is a 4x4 block of RGBA8, row-major (64 bytes); writes rhi::BlockByteSize(Format()) bytes.
        virtual void EncodeBlock(std::span<const std::uint8_t, 64> texels, std::byte *destination) const = 0;
    };

    // The encoder for `format`, or null. Built-in encoders cover BC1, BC3, BC4 and BC5.
    [[nodiscard]] std::shared_ptr<const TextureBlockEncoder> FindTextureBlockEncoder(rhi::Format format);
    // Adds an encoder, replacing any earlier one for its format.
    void RegisterTextureBlockEncoder(std::shared_ptr<const TextureBlockEncoder> encoder);

    using SampledFormatQuery = std::function<bool(rhi::Format)>;
    // The preferred block-compressed format for `request` that `deviceSupports` accepts and an encoder exists for;
    // nullopt keeps the texture uncompressed.
    [[nodiscard]] std::optional<rhi::Format> SelectCompressedFormat(const TextureCompressionRequest &request,
                                                                    const SampledFormatQuery &deviceSupports);

    [[nodiscard]] bool TextureUsesAlpha(std::span<const std::byte> rgba8);

    // The full RGBA8 mip chain of `semantic` texels: sRGB colour averages in linear light, data averages its bytes,
    // tangent normals keep their averaged length (rhi::BuildNormalMipmaps).
    [[nodiscard]] std::vector<std::byte> BuildTextureMipChain(std::span<const std::byte> rgba8, std::uint32_t width,
                                                              std::uint32_t height, TextureSemantic semantic, bool srgb);

    // Builds `semantic`'s mip chain and encodes every level in `format`. nullopt when no encoder is registered for
    // `format` or the input is not width x height RGBA8.
    [[nodiscard]] std::optional<EncodedTexture> EncodeTexture(std::span<const std::byte> rgba8, std::uint32_t width,
                                                              std::uint32_t height, TextureSemantic semantic,
                                                              rhi::Format format);
}
