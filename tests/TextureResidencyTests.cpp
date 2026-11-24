// Texture loading and CPU residency: the shared image decoder (ImageDecoding.h), decodes started ahead of use
// (TextureManager::PrefetchTextureFiles), file textures releasing their CPU pixels once uploaded and reading them back
// (Texture, TexturePixelRetention), how material slots are sampled (GetMaterialTextureSampling) and cooking a
// project's material textures (assets/TextureCook.h), and the source index that lets indexed files load without
// decoding (TextureSourceIndex.h).
#include "PlutoGE/assets/TextureCook.h"
#include "PlutoGE/render/ImageDecoding.h"
#include "PlutoGE/render/MaterialTextureSlots.h"
#include "PlutoGE/render/TextureCooking.h"
#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/TextureSourceIndex.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    }

    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-texture-residency-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root); }
        ~Scratch() { std::error_code error; std::filesystem::remove_all(root, error); }
    };

    // A 24-bit BMP whose texel (x, y) is texel(x, y) (RGB).
    void WriteBmp(const std::filesystem::path &path, int width, int height, auto texel)
    {
        const int row = (width * 3 + 3) & ~3;
        const std::uint32_t size = 54 + static_cast<std::uint32_t>(row * height);
        std::vector<unsigned char> bytes(size, 0);
        const auto put = [&](std::size_t at, std::uint32_t value) {
            for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<unsigned char>(value >> (8 * i));
        };
        bytes[0] = 'B'; bytes[1] = 'M';
        put(2, size); put(10, 54); put(14, 40); put(18, static_cast<std::uint32_t>(width)); put(22, static_cast<std::uint32_t>(height));
        bytes[26] = 1; bytes[28] = 24; put(34, static_cast<std::uint32_t>(row * height));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                // BMP rows are stored bottom-up, BGR.
                const auto rgb = texel(x, y);
                auto *pixel = &bytes[54 + static_cast<std::size_t>(height - 1 - y) * row + static_cast<std::size_t>(x) * 3];
                pixel[0] = rgb[2]; pixel[1] = rgb[1]; pixel[2] = rgb[0];
            }
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(output), "Could not write a BMP fixture");
    }

    auto Gradient()
    {
        return [](int x, int y) { return std::array<unsigned char, 3>{static_cast<unsigned char>(x), static_cast<unsigned char>(y), 77}; };
    }

    void Decoding(const Scratch &scratch)
    {
        const auto path = scratch.root / "decode.bmp";
        WriteBmp(path, 3, 2, Gradient());
        const auto image = render::DecodeImageFile(path.string());
        Require(image && image->width == 3 && image->height == 2 && image->channels == 3 && image->rgba8.size() == 24,
                "A BMP decodes to RGBA8 with its stored channel count");
        Require(image->rgba8[4 * 4 + 0] == 1 && image->rgba8[4 * 4 + 1] == 1 && image->rgba8[4 * 4 + 2] == 77 &&
                    image->rgba8[4 * 4 + 3] == 255,
                "Texels are top row first, with opaque alpha");
        Require(!render::DecodeImageFile((scratch.root / "missing.png").string()), "A missing file decodes to nothing");
        const std::vector<unsigned char> grey{9, 200};
        const auto expanded = render::ExpandPixelsToRgba8(grey.data(), 2, 1, 1);
        Require(expanded == std::vector<unsigned char>{9, 9, 9, 255, 200, 200, 200, 255}, "Grey expands into RGB");
    }

    void Prefetching(const Scratch &scratch)
    {
        const auto path = (scratch.root / "prefetch.bmp").string();
        WriteBmp(path, 4, 4, Gradient());
        render::TextureManager textures(true);
        textures.PrefetchTextureFiles(std::vector<std::string>{path, (scratch.root / "absent.bmp").string()});
        auto *texture = textures.LoadTextureFromFile(path.c_str(), render::TextureColorSpace::SRGB);
        Require(texture && texture->GetWidth() == 4 && texture->GetRgba8Pixels().size() == 64, "A prefetched file loads whole");
        Require(texture->GetRgba8Pixels()[(4 * 2 + 3) * 4 + 1] == 2, "Prefetched texels are the file's");
        Require(!textures.LoadTextureFromFile((scratch.root / "absent.bmp").string().c_str()), "A prefetched missing file still fails");
        // Loaded files are not decoded again.
        textures.PrefetchTextureFiles(std::vector<std::string>{path});
        Require(textures.LoadTextureFromFile(path.c_str(), render::TextureColorSpace::SRGB) == texture, "Loaded textures are cached");
    }

    void Release(const Scratch &scratch)
    {
        const auto path = (scratch.root / "release.bmp").string();
        WriteBmp(path, 8, 8, Gradient());
        render::TextureManager textures(true);
        auto *texture = textures.LoadTextureFromFile(path.c_str());
        Require(texture && texture->CanReleasePixels() && texture->HasResidentPixels(), "File textures can release their pixels");
        const std::vector<unsigned char> original(texture->GetRgba8Pixels().begin(), texture->GetRgba8Pixels().end());

        texture->MarkUploaded();
        Require(textures.ReleaseUploadedPixels() == 0 && texture->HasResidentPixels(), "Keep retains every texture's pixels");
        textures.SetPixelRetention(render::TexturePixelRetention::ReleaseAfterUpload);
        const auto reader = texture->AcquireRgba8Pixels();
        Require(textures.ReleaseUploadedPixels() == 8 * 8 * 4 && !texture->HasResidentPixels(), "Uploaded file textures release");
        Require(reader && *reader == original, "A reader holding the pixels keeps them through the release");
        const auto readBack = texture->GetRgba8Pixels();
        Require(std::vector<unsigned char>(readBack.begin(), readBack.end()) == original && texture->HasResidentPixels(),
                "Released pixels read back from the file");
        Require(texture->CopyRgba8Bytes().size() == original.size(), "Renderers copy the read-back pixels");

        // Content changed since the upload: kept until uploaded again.
        Require(texture->SetRgba8Pixels(original) && !texture->IsUploaded() && textures.ReleaseUploadedPixels() == 0,
                "New content is kept until it is uploaded");

        // Generated textures have no file to read back from: never released.
        auto generated = render::Texture::CreateRgba8(2, 2, std::vector<unsigned char>(16, 5));
        generated->MarkUploaded();
        Require(!generated->CanReleasePixels() && !generated->ReleaseUploadedPixels() && generated->HasResidentPixels(),
                "Generated textures keep their pixels");

        // Readers on other threads while the owner releases.
        texture->MarkUploaded();
        std::atomic<bool> stop{false}, mismatch{false};
        std::thread reading([&] {
            while (!stop)
                if (const auto pixels = texture->AcquireRgba8Pixels(); !pixels || *pixels != original)
                    mismatch = true;
        });
        for (int round = 0; round < 200; ++round)
        {
            texture->MarkUploaded();
            (void)textures.ReleaseUploadedPixels();
        }
        stop = true;
        reading.join();
        Require(!mismatch, "Readers on other threads always see the whole texture");
    }

    void Sampling()
    {
        using render::MaterialTextureSlot;
        using render::TextureSemantic;
        const auto albedo = render::GetMaterialTextureSampling(MaterialTextureSlot::Albedo, false);
        const auto normal = render::GetMaterialTextureSampling(MaterialTextureSlot::Normal, false);
        const auto roughness = render::GetMaterialTextureSampling(MaterialTextureSlot::Roughness, false);
        const auto emission = render::GetMaterialTextureSampling(MaterialTextureSlot::Emission, false);
        const auto emissionMask = render::GetMaterialTextureSampling(MaterialTextureSlot::Emission, true);
        const auto sheen = render::GetMaterialTextureSampling(MaterialTextureSlot::SheenColor, false);
        Require(albedo.semantic == TextureSemantic::Color && albedo.srgb, "Albedo is sRGB colour");
        Require(normal.semantic == TextureSemantic::TangentNormal && !normal.srgb, "Normals are tangent normals");
        Require(roughness.semantic == TextureSemantic::Data && !roughness.srgb, "Roughness is data");
        Require(emission.semantic == TextureSemantic::Color && emission.srgb, "Emission is sRGB colour");
        Require(emissionMask.semantic == TextureSemantic::Data && !emissionMask.srgb, "Emission masks are data");
        Require(sheen.semantic == TextureSemantic::Color && sheen.srgb, "Sheen colour is sRGB colour");
    }

    void Cooking(const Scratch &scratch)
    {
        const auto project = scratch.root / "project";
        WriteBmp(project / "Assets/Textures/Bark.bmp", 256, 256, Gradient());
        WriteBmp(project / "Assets/Textures/BarkNormal.bmp", 256, 256, [](int, int) { return std::array<unsigned char, 3>{128, 128, 255}; });
        WriteBmp(project / "Assets/Textures/Tiny.bmp", 8, 8, Gradient());
        {
            std::ofstream material(project / "Assets/Bark.plutomaterial");
            material << "Color=1,1,1,1\nAlbedoTexture=Textures/Bark.bmp\r\nNormalTexture=Textures/BarkNormal.bmp\n"
                        "RoughnessTexture=Textures/Bark.bmp\nOcclusionTexture=Textures/Tiny.bmp\nMetallicTexture=Textures/Missing.bmp\n";
            std::ofstream other(project / "Assets/Other.plutomaterial");
            other << "AlbedoTexture=Textures/Bark.bmp\nEmissionTexture=\n";
        }
        assets::TextureCookStats stats;
        std::string error;
        Require(assets::CookMaterialTextures(project, "Assets", 1, &stats, &error), error.c_str());
        // Bark is sampled as colour (albedo) and data (roughness); the normal map as normals; Tiny stays RGBA8.
        Require(stats.materials == 2 && stats.textures == 3, "Every material's existing textures are found once");
        Require(stats.encoded == 3 && stats.uncompressed == 1 && stats.failed == 0, "Each sampling is encoded once");
        assets::TextureCookStats again;
        Require(assets::CookMaterialTextures(project, "Assets", 1, &again, &error) && again.encoded == 0 && again.alreadyCooked == 3,
                "A second cook reuses the store");
        Require(std::filesystem::exists(project / render::kCookedTextureDirectory), "The store is under the project root");

        // The cook's source index names each cooked file, relative to the project root, so the game finds them.
        const render::TextureSourceIndex cooked({}, {{render::TextureSourceIndex::DirectoryBeside(project / render::kCookedTextureDirectory), project}});
        const auto bark = project / "Assets/Textures/Bark.bmp";
        const auto barkVersion = render::TextureSourceIndex::CurrentVersion(bark);
        const auto barkRecord = barkVersion ? cooked.Find(bark, *barkVersion) : std::nullopt;
        Require(barkRecord && barkRecord->width == 256 && barkRecord->samplings.size() == 2, "A cooked file's samplings are indexed");
        const auto tiny = project / "Assets/Textures/Tiny.bmp";
        Require(!cooked.Find(tiny, *render::TextureSourceIndex::CurrentVersion(tiny)), "Uncompressed textures are not indexed");
    }

    void SourceIndex(const Scratch &scratch)
    {
        const auto file = scratch.root / "indexed.bmp";
        WriteBmp(file, 8, 4, Gradient());
        const auto version = render::TextureSourceIndex::CurrentVersion(file);
        Require(version && version->size > 0, "A file has a version");
        const auto shared = std::make_shared<render::TextureSourceIndex>(scratch.root / "index");
        const render::TextureSourceSampling colour{render::TextureSemantic::Color, true, 7, render::rhi::Format::BC1RgbaSrgb, {}};
        const render::TextureSourceSampling data{render::TextureSemantic::Data, false, 7, render::rhi::Format::BC4RUnorm, {}};
        Require(shared->Record(file, *version, 8, 4, 3, {}, colour) && shared->Record(file, *version, 8, 4, 3, {}, data),
                "Samplings are recorded");
        // A fresh index (another session) reads both back from disk.
        const render::TextureSourceIndex reread(scratch.root / "index");
        const auto record = reread.Find(file, *version);
        Require(record && record->width == 8 && record->height == 4 && record->channels == 3 && record->samplings.size() == 2,
                "Records persist with every sampling");
        Require(record->Find(render::TextureSemantic::Data, false, 7) && !record->Find(render::TextureSemantic::Data, false, 8),
                "A sampling recorded under another policy does not match");
        auto changed = *version;
        ++changed.modified;
        Require(!reread.Find(file, changed), "Another version of the file is unknown");

        // Loading an indexed file decodes nothing until its pixels are read.
        render::TextureManager textures(true);
        textures.SetSourceIndex(shared);
        auto *texture = textures.LoadTextureFromFile(file.string().c_str(), render::TextureColorSpace::SRGB);
        Require(texture && texture->GetWidth() == 8 && texture->GetHeight() == 4 && !texture->HasResidentPixels(),
                "An indexed file loads without decoding");
        Require(texture->GetFileVersion() == version, "It holds its file's content");
        Require(texture->GetRgba8Pixels().size() == 8 * 4 * 4 && texture->HasResidentPixels(), "Its pixels decode when read");
        Require(texture->SetRgba8Pixels(std::vector<unsigned char>(8 * 4 * 4, 1)) && !texture->GetFileVersion(),
                "New pixels are no longer the file's content");
    }
}

int main()
try
{
    Scratch scratch;
    Decoding(scratch);
    Prefetching(scratch);
    Release(scratch);
    Sampling();
    Cooking(scratch);
    SourceIndex(scratch);
    std::cout << "Texture residency tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
