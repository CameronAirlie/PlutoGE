#include "PlutoGE/render/TextureCompression.h"

#include "PlutoGE/render/rhi/FormatInfo.h"
#include "rhi/NormalMipmaps.h"

#include <stb_dxt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace PlutoGE::render
{
    namespace
    {
        // stb_dxt encoders. HIGHQUAL runs two endpoint refinement passes; textures are encoded once and cached.
        class StbColorBlockEncoder final : public TextureBlockEncoder
        {
        public:
            explicit StbColorBlockEncoder(rhi::Format format) : m_format(format) {}
            rhi::Format Format() const noexcept override { return m_format; }
            std::string_view Identity() const noexcept override { return "stb_dxt 1.12 highqual"; }
            void EncodeBlock(std::span<const std::uint8_t, 64> texels, std::byte *destination) const override
            {
                const int alpha = rhi::BlockByteSize(m_format) == 16 ? 1 : 0;
                stb_compress_dxt_block(reinterpret_cast<unsigned char *>(destination), texels.data(), alpha, STB_DXT_HIGHQUAL);
            }

        private:
            rhi::Format m_format;
        };

        class StbBc4BlockEncoder final : public TextureBlockEncoder
        {
        public:
            rhi::Format Format() const noexcept override { return rhi::Format::BC4RUnorm; }
            std::string_view Identity() const noexcept override { return "stb_dxt 1.12 bc4"; }
            void EncodeBlock(std::span<const std::uint8_t, 64> texels, std::byte *destination) const override
            {
                std::array<unsigned char, 16> red{};
                for (std::size_t i = 0; i < 16; ++i)
                    red[i] = texels[i * 4];
                stb_compress_bc4_block(reinterpret_cast<unsigned char *>(destination), red.data());
            }
        };

        class StbBc5BlockEncoder final : public TextureBlockEncoder
        {
        public:
            rhi::Format Format() const noexcept override { return rhi::Format::BC5RgUnorm; }
            std::string_view Identity() const noexcept override { return "stb_dxt 1.12 bc5"; }
            void EncodeBlock(std::span<const std::uint8_t, 64> texels, std::byte *destination) const override
            {
                std::array<unsigned char, 32> redGreen{};
                for (std::size_t i = 0; i < 16; ++i)
                {
                    redGreen[i * 2] = texels[i * 4];
                    redGreen[i * 2 + 1] = texels[i * 4 + 1];
                }
                stb_compress_bc5_block(reinterpret_cast<unsigned char *>(destination), redGreen.data());
            }
        };

        struct EncoderRegistry
        {
            std::mutex mutex;
            std::unordered_map<rhi::Format, std::shared_ptr<const TextureBlockEncoder>> encoders;

            EncoderRegistry()
            {
                for (const auto format : {rhi::Format::BC1RgbaUnorm, rhi::Format::BC1RgbaSrgb, rhi::Format::BC3RgbaUnorm,
                                          rhi::Format::BC3RgbaSrgb})
                    encoders[format] = std::make_shared<StbColorBlockEncoder>(format);
                encoders[rhi::Format::BC4RUnorm] = std::make_shared<StbBc4BlockEncoder>();
                encoders[rhi::Format::BC5RgUnorm] = std::make_shared<StbBc5BlockEncoder>();
            }
        };

        EncoderRegistry &Registry()
        {
            static EncoderRegistry registry;
            return registry;
        }

        const std::array<float, 256> &SrgbToLinearTable()
        {
            static const auto table = [] {
                std::array<float, 256> values{};
                for (std::size_t i = 0; i < values.size(); ++i)
                {
                    const float c = float(i) / 255.0f;
                    values[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                return values;
            }();
            return table;
        }

        std::uint8_t LinearToSrgbByte(float value)
        {
            value = std::clamp(value, 0.0f, 1.0f);
            const float encoded = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(encoded * 255.0f));
        }

        // Halves an RGBA8 level with a 2x2 box (clamped at odd edges), as the GPU mip generator does.
        void AppendHalvedLevel(std::vector<std::byte> &chain, std::size_t sourceOffset, std::uint32_t width,
                               std::uint32_t height, bool srgb)
        {
            const auto nextWidth = std::max(1u, width / 2), nextHeight = std::max(1u, height / 2);
            const auto destinationOffset = chain.size();
            chain.resize(destinationOffset + std::size_t(nextWidth) * nextHeight * 4);
            const auto &toLinear = SrgbToLinearTable();
            const auto *source = reinterpret_cast<const std::uint8_t *>(chain.data() + sourceOffset);
            auto *destination = reinterpret_cast<std::uint8_t *>(chain.data() + destinationOffset);
            for (std::uint32_t y = 0; y < nextHeight; ++y)
                for (std::uint32_t x = 0; x < nextWidth; ++x)
                {
                    const std::uint32_t xs[2]{std::min(width - 1, x * 2), std::min(width - 1, x * 2 + 1)};
                    const std::uint32_t ys[2]{std::min(height - 1, y * 2), std::min(height - 1, y * 2 + 1)};
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        float linearSum = 0.0f;
                        unsigned sum = 0;
                        for (const auto sy : ys)
                            for (const auto sx : xs)
                            {
                                const auto value = source[(std::size_t(sy) * width + sx) * 4 + channel];
                                sum += value;
                                linearSum += toLinear[value];
                            }
                        destination[(std::size_t(y) * nextWidth + x) * 4 + channel] =
                            srgb && channel < 3 ? LinearToSrgbByte(linearSum * 0.25f) : static_cast<std::uint8_t>((sum + 2) / 4);
                    }
                }
        }

        void EncodeLevel(const TextureBlockEncoder &encoder, const std::uint8_t *texels, std::uint32_t width,
                         std::uint32_t height, std::byte *destination)
        {
            const auto blockBytes = rhi::BlockByteSize(encoder.Format());
            std::array<std::uint8_t, 64> block{};
            for (std::uint32_t by = 0; by < (height + 3) / 4; ++by)
                for (std::uint32_t bx = 0; bx < (width + 3) / 4; ++bx)
                {
                    // Partial edge blocks repeat the last row and column; those texels are never sampled.
                    for (std::uint32_t y = 0; y < 4; ++y)
                        for (std::uint32_t x = 0; x < 4; ++x)
                        {
                            const auto sx = std::min(width - 1, bx * 4 + x), sy = std::min(height - 1, by * 4 + y);
                            std::memcpy(block.data() + (y * 4 + x) * 4, texels + (std::size_t(sy) * width + sx) * 4, 4);
                        }
                    encoder.EncodeBlock(block, destination);
                    destination += blockBytes;
                }
        }
    }

    TextureCompressionSettings TextureCompressionSettings::FromEnvironment()
    {
        TextureCompressionSettings settings;
        if (const char *value = std::getenv("PLUTO_TEXTURE_COMPRESSION"); value && std::string_view(value) == "0")
            settings.enabled = false;
        std::error_code error;
        const auto temporary = std::filesystem::temp_directory_path(error);
        if (!error)
            settings.cacheDirectory = temporary / "PlutoGE" / "TextureCompression";
        return settings;
    }

    std::shared_ptr<const TextureBlockEncoder> FindTextureBlockEncoder(rhi::Format format)
    {
        auto &registry = Registry();
        std::lock_guard lock(registry.mutex);
        const auto found = registry.encoders.find(format);
        return found != registry.encoders.end() ? found->second : nullptr;
    }

    void RegisterTextureBlockEncoder(std::shared_ptr<const TextureBlockEncoder> encoder)
    {
        if (!encoder || !rhi::IsBlockCompressed(encoder->Format()))
            return;
        auto &registry = Registry();
        std::lock_guard lock(registry.mutex);
        registry.encoders[encoder->Format()] = std::move(encoder);
    }

    std::optional<rhi::Format> SelectCompressedFormat(const TextureCompressionRequest &request,
                                                      const SampledFormatQuery &deviceSupports)
    {
        using rhi::Format;
        // In order of preference. Opaque colour and data take BC1 (half BC7's size); with alpha, BC7 when an encoder
        // is registered, else BC3. Normals keep two channels at full BC5 precision.
        std::array<Format, 3> candidates{};
        std::size_t count = 0;
        if (request.semantic == TextureSemantic::TangentNormal)
            candidates[count++] = Format::BC5RgUnorm;
        else if (!request.usesAlpha)
            candidates[count++] = request.srgb ? Format::BC1RgbaSrgb : Format::BC1RgbaUnorm;
        else
        {
            candidates[count++] = request.srgb ? Format::BC7RgbaSrgb : Format::BC7RgbaUnorm;
            candidates[count++] = request.srgb ? Format::BC3RgbaSrgb : Format::BC3RgbaUnorm;
        }
        for (std::size_t i = 0; i < count; ++i)
            if ((!deviceSupports || deviceSupports(candidates[i])) && FindTextureBlockEncoder(candidates[i]))
                return candidates[i];
        return std::nullopt;
    }

    bool TextureUsesAlpha(std::span<const std::byte> rgba8)
    {
        for (std::size_t i = 3; i < rgba8.size(); i += 4)
            if (rgba8[i] != std::byte{255})
                return true;
        return false;
    }

    std::vector<std::byte> BuildTextureMipChain(std::span<const std::byte> rgba8, std::uint32_t width, std::uint32_t height,
                                                TextureSemantic semantic, bool srgb)
    {
        const auto levels = rhi::FullMipLevelCount(width, height);
        if (semantic == TextureSemantic::TangentNormal)
            return rhi::BuildNormalMipmaps(rgba8, width, height, levels);
        std::vector<std::byte> chain(rgba8.begin(), rgba8.end());
        chain.reserve(rhi::TextureChainByteSize(rhi::Format::R8G8B8A8Unorm, width, height, levels));
        std::size_t offset = 0;
        for (std::uint32_t level = 1; level < levels; ++level)
        {
            AppendHalvedLevel(chain, offset, width, height, srgb && semantic == TextureSemantic::Color);
            offset += std::size_t(width) * height * 4;
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        return chain;
    }

    std::optional<EncodedTexture> EncodeTexture(std::span<const std::byte> rgba8, std::uint32_t width, std::uint32_t height,
                                                TextureSemantic semantic, rhi::Format format)
    {
        const auto encoder = FindTextureBlockEncoder(format);
        if (!encoder || !width || !height || rgba8.size() != std::size_t(width) * height * 4)
            return std::nullopt;
        const auto chain = BuildTextureMipChain(rgba8, width, height, semantic, rhi::IsSrgb(format));
        EncodedTexture encoded;
        encoded.format = format;
        encoded.width = width;
        encoded.height = height;
        encoded.mipLevels = rhi::FullMipLevelCount(width, height);
        encoded.data.resize(rhi::TextureChainByteSize(format, width, height, encoded.mipLevels));
        std::size_t sourceOffset = 0, destinationOffset = 0;
        for (std::uint32_t level = 0; level < encoded.mipLevels; ++level)
        {
            EncodeLevel(*encoder, reinterpret_cast<const std::uint8_t *>(chain.data() + sourceOffset), width, height,
                        encoded.data.data() + destinationOffset);
            sourceOffset += std::size_t(width) * height * 4;
            destinationOffset += rhi::TextureLevelByteSize(format, width, height);
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        return encoded;
    }
}
