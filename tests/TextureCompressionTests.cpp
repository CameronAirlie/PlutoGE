// Block compression of material textures: format traits, format policy, mip chains, encoders, the content-addressed
// store and the background queue (TextureCompression.h, CompressedTextureStore.h, TextureCompressionQueue.h).
#include "CompressedTextureStore.h"
#include "TextureCompressionQueue.h"
#include "PlutoGE/render/DdsImage.h"
#include "PlutoGE/render/TextureCompression.h"
#include "PlutoGE/render/rhi/FormatInfo.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::vector<std::byte> Image(std::uint32_t width, std::uint32_t height, auto texel)
    {
        std::vector<std::byte> pixels(std::size_t(width) * height * 4);
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const auto value = texel(x, y);
                for (int channel = 0; channel < 4; ++channel)
                    pixels[(std::size_t(y) * width + x) * 4 + channel] = std::byte(value[channel]);
            }
        return pixels;
    }

    // Decodes level 0 of an encoded chain through the engine's DDS decoder (BC1, BC3 and BC5).
    std::vector<unsigned char> DecodeLevel0(const EncodedTexture &texture)
    {
        std::vector<unsigned char> file(128, 0);
        const auto put = [&](std::size_t offset, std::uint32_t value) {
            for (int i = 0; i < 4; ++i)
                file[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        };
        put(0, 0x20534444);
        put(4, 124);
        put(12, texture.height);
        put(16, texture.width);
        put(76, 32);
        put(80, 4);
        const auto fourCc = texture.format == rhi::Format::BC5RgUnorm ? 0x32495441u
                            : rhi::BlockByteSize(texture.format) == 8 ? 0x31545844u
                                                                       : 0x35545844u;
        put(84, fourCc);
        const auto level = rhi::TextureLevelByteSize(texture.format, texture.width, texture.height);
        file.insert(file.end(), reinterpret_cast<const unsigned char *>(texture.data.data()),
                    reinterpret_cast<const unsigned char *>(texture.data.data()) + level);
        DdsImage image;
        Require(DecodeDds(file, image), "Encoded level did not decode");
        return image.pixels;
    }

    double MeanError(const std::vector<unsigned char> &decoded, const std::vector<std::byte> &source, int channels)
    {
        double total = 0;
        for (std::size_t i = 0; i < decoded.size(); ++i)
            if (int(i % 4) < channels)
                total += std::abs(int(decoded[i]) - std::to_integer<int>(source[i]));
        return total / (decoded.size() / 4 * channels);
    }

    void CheckFormatTraits()
    {
        using rhi::Format;
        Require(rhi::IsBlockCompressed(Format::BC1RgbaSrgb) && !rhi::IsBlockCompressed(Format::R8G8B8A8Srgb), "Compression trait");
        Require(rhi::TextureLevelByteSize(Format::BC1RgbaUnorm, 5, 5) == 2 * 2 * 8, "Partial BC1 blocks round up");
        Require(rhi::TextureLevelByteSize(Format::BC5RgUnorm, 1, 1) == 16, "A 1x1 level is one block");
        Require(rhi::TextureLevelByteSize(Format::R8G8B8A8Unorm, 3, 2) == 24, "RGBA8 level size");
        Require(rhi::FullMipLevelCount(256, 64) == 9, "Full chain of 256x64");
        // 8x8 BC1: 4 blocks, then 1, then 1 (4x4, 2x2 and 1x1 are each one block).
        Require(rhi::TextureChainByteSize(Format::BC1RgbaUnorm, 8, 8, 4) == (4 + 1 + 1 + 1) * 8, "BC1 chain size");
        Require(rhi::IsSrgb(Format::BC3RgbaSrgb) && !rhi::IsSrgb(Format::BC5RgUnorm), "sRGB trait");
    }

    class FakeBc7Encoder final : public TextureBlockEncoder
    {
    public:
        explicit FakeBc7Encoder(rhi::Format format) : m_format(format) {}
        rhi::Format Format() const noexcept override { return m_format; }
        std::string_view Identity() const noexcept override { return "fake bc7"; }
        void EncodeBlock(std::span<const std::uint8_t, 64>, std::byte *destination) const override
        {
            std::fill_n(destination, 16, std::byte{0});
        }

    private:
        rhi::Format m_format;
    };

    void CheckPolicy()
    {
        using rhi::Format;
        const auto all = [](Format) { return true; };
        const auto none = [](Format) { return false; };
        Require(SelectCompressedFormat({TextureSemantic::Color, true, false}, all) == Format::BC1RgbaSrgb, "Opaque colour takes BC1");
        Require(SelectCompressedFormat({TextureSemantic::Color, true, true}, all) == Format::BC3RgbaSrgb,
                "Colour with alpha takes BC3 without a BC7 encoder");
        Require(SelectCompressedFormat({TextureSemantic::Data, false, false}, all) == Format::BC1RgbaUnorm, "Opaque data takes BC1");
        Require(SelectCompressedFormat({TextureSemantic::TangentNormal, false, false}, all) == Format::BC5RgUnorm, "Normals take BC5");
        Require(!SelectCompressedFormat({TextureSemantic::Color, true, false}, none), "Unsupported formats stay RGBA8");
        Require(SelectCompressedFormat({TextureSemantic::Color, true, true},
                                       [](Format format) { return format != Format::BC3RgbaSrgb; }) == std::nullopt,
                "No supported candidate stays RGBA8");
        // Registering an encoder makes its format available without touching the policy's callers.
        RegisterTextureBlockEncoder(std::make_shared<FakeBc7Encoder>(Format::BC7RgbaSrgb));
        Require(SelectCompressedFormat({TextureSemantic::Color, true, true}, all) == Format::BC7RgbaSrgb,
                "A registered BC7 encoder is preferred for alpha");
        Require(SelectCompressedFormat({TextureSemantic::Color, true, false}, all) == Format::BC1RgbaSrgb,
                "Opaque colour keeps the smaller BC1");
    }

    void CheckMipChain()
    {
        // A black and white checker averages to linear 0.5 in sRGB (188) and to 128 as data.
        const auto checker = Image(2, 2, [](auto x, auto y) {
            const unsigned char v = ((x + y) & 1) ? 255 : 0;
            return std::array<unsigned char, 4>{v, v, v, 255};
        });
        const auto colour = BuildTextureMipChain(checker, 2, 2, TextureSemantic::Color, true);
        const auto data = BuildTextureMipChain(checker, 2, 2, TextureSemantic::Data, false);
        Require(colour.size() == (4 + 1) * 4 && data.size() == colour.size(), "Mip chain size");
        Require(std::abs(std::to_integer<int>(colour[16]) - 188) <= 1, "sRGB colour mips average in linear light");
        Require(std::abs(std::to_integer<int>(data[16]) - 128) <= 1, "Data mips average bytes");
        Require(std::to_integer<int>(colour[19]) == 255, "Alpha averages as data");
    }

    void CheckEncoding()
    {
        const auto gradient = Image(64, 64, [](auto x, auto y) {
            return std::array<unsigned char, 4>{static_cast<unsigned char>(x * 4), static_cast<unsigned char>(y * 4),
                                                static_cast<unsigned char>(128), static_cast<unsigned char>((x * 4) ^ (y * 4))};
        });
        const auto bc1 = EncodeTexture(gradient, 64, 64, TextureSemantic::Color, rhi::Format::BC1RgbaSrgb);
        Require(bc1 && bc1->mipLevels == 7 &&
                    bc1->data.size() == rhi::TextureChainByteSize(rhi::Format::BC1RgbaSrgb, 64, 64, 7),
                "BC1 chain layout");
        // Both red and green vary within each block, which a BC1 endpoint line cannot follow exactly (~3.2 here).
        Require(MeanError(DecodeLevel0(*bc1), gradient, 3) < 4.5, "BC1 colour error");
        const auto flat = Image(16, 16, [](auto, auto) { return std::array<unsigned char, 4>{200, 120, 40, 255}; });
        const auto flatBc1 = EncodeTexture(flat, 16, 16, TextureSemantic::Color, rhi::Format::BC1RgbaSrgb);
        Require(flatBc1 && MeanError(DecodeLevel0(*flatBc1), flat, 3) < 2.0, "BC1 reproduces a flat colour");
        const auto bc3 = EncodeTexture(gradient, 64, 64, TextureSemantic::Data, rhi::Format::BC3RgbaUnorm);
        Require(bc3 && MeanError(DecodeLevel0(*bc3), gradient, 4) < 6.0, "BC3 error including alpha");

        const auto normals = Image(64, 64, [](auto x, auto y) {
            const float nx = std::sin(x * 0.2f) * 0.5f, ny = std::cos(y * 0.15f) * 0.5f;
            const float nz = std::sqrt(1 - nx * nx - ny * ny);
            return std::array<unsigned char, 4>{static_cast<unsigned char>(std::lround((nx * .5f + .5f) * 255)),
                                                static_cast<unsigned char>(std::lround((ny * .5f + .5f) * 255)),
                                                static_cast<unsigned char>(std::lround((nz * .5f + .5f) * 255)), 255};
        });
        const auto bc5 = EncodeTexture(normals, 64, 64, TextureSemantic::TangentNormal, rhi::Format::BC5RgUnorm);
        Require(bc5 && MeanError(DecodeLevel0(*bc5), normals, 2) < 1.5, "BC5 keeps normal X and Y precisely");
        Require(!EncodeTexture(normals, 63, 64, TextureSemantic::Data, rhi::Format::BC1RgbaUnorm), "Size mismatch is rejected");
        // Non-multiple-of-four sizes encode partial edge blocks.
        const auto odd = Image(10, 6, [](auto, auto) { return std::array<unsigned char, 4>{10, 20, 30, 255}; });
        const auto oddEncoded = EncodeTexture(odd, 10, 6, TextureSemantic::Data, rhi::Format::BC1RgbaUnorm);
        Require(oddEncoded && oddEncoded->data.size() == rhi::TextureChainByteSize(rhi::Format::BC1RgbaUnorm, 10, 6, 4),
                "Odd-sized chain layout");
    }

    void CheckStoreAndQueue()
    {
        const auto directory = std::filesystem::temp_directory_path() / "PlutoGETextureCompressionTests" /
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto pixels = Image(32, 32, [](auto x, auto y) {
            return std::array<unsigned char, 4>{static_cast<unsigned char>(x * 8), static_cast<unsigned char>(y * 8), 0, 255};
        });
        auto store = std::make_shared<CompressedTextureStore>(directory);
        const auto key = CompressedTextureStore::Key(pixels, 32, 32, TextureSemantic::Color, rhi::Format::BC1RgbaSrgb);
        Require(key != CompressedTextureStore::Key(pixels, 32, 32, TextureSemantic::Color, rhi::Format::BC3RgbaSrgb),
                "Keys depend on the format");
        Require(key != CompressedTextureStore::Key(pixels, 32, 32, TextureSemantic::Data, rhi::Format::BC1RgbaSrgb),
                "Keys depend on the semantic");
        Require(!store->Load(key), "An empty store misses");
        const auto encoded = EncodeTexture(pixels, 32, 32, TextureSemantic::Color, rhi::Format::BC1RgbaSrgb);
        Require(encoded && store->Save(key, *encoded), "Store save");
        const auto loaded = store->Load(key);
        Require(loaded && loaded->data == encoded->data && loaded->format == encoded->format &&
                    loaded->mipLevels == encoded->mipLevels,
                "Store round trip");
        // A truncated entry reads as a miss.
        for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
            if (entry.is_regular_file())
                std::filesystem::resize_file(entry.path(), 40);
        Require(!store->Load(key), "Damaged entries miss");

        TextureCompressionQueue queue(store, 2);
        queue.Submit({7, pixels, 32, 32, TextureSemantic::Color, rhi::Format::BC1RgbaSrgb});
        queue.Submit({8, std::vector<std::byte>(12), 32, 32, TextureSemantic::Color, rhi::Format::BC1RgbaSrgb});
        std::vector<TextureCompressionQueue::Result> results;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (results.size() < 2 && std::chrono::steady_clock::now() < deadline)
        {
            for (auto &result : queue.TakeCompleted())
                results.push_back(std::move(result));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Require(results.size() == 2 && queue.Outstanding() == 0, "Queue completes every job");
        for (const auto &result : results)
            Require(result.ticket == 7 ? result.texture && result.texture->data == encoded->data : !result.texture,
                    "Queue results match synchronous encodes; invalid jobs fail");
        const auto again = TextureCompressionQueue::Run({9, pixels, 32, 32, TextureSemantic::Color, rhi::Format::BC1RgbaSrgb},
                                                        store.get());
        Require(again.fromCache && again.texture && again.texture->data == encoded->data, "Second encode reads the store");
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
}

int main()
{
    try
    {
        CheckFormatTraits();
        CheckMipChain();
        CheckEncoding();
        CheckStoreAndQueue();
        CheckPolicy(); // Last: it registers a fake BC7 encoder.
        std::cout << "Texture compression tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
