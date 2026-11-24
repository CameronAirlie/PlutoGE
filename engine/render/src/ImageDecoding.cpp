#include "PlutoGE/render/ImageDecoding.h"

#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/DdsImage.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <memory>

#include <stb_image.h>

namespace PlutoGE::render
{
    namespace
    {
        bool IsDds(const std::string &path)
        {
            auto extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return extension == ".dds";
        }
    }

    std::vector<unsigned char> ExpandPixelsToRgba8(const unsigned char *pixels, int width, int height, int channels)
    {
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<unsigned char> rgba(pixelCount * 4, 255);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const std::size_t source = pixel * static_cast<std::size_t>(channels);
            const std::size_t destination = pixel * 4;
            rgba[destination] = pixels[source];
            rgba[destination + 1] = channels >= 2 ? pixels[source + 1] : pixels[source];
            rgba[destination + 2] = channels >= 3 ? pixels[source + 2] : pixels[source];
            rgba[destination + 3] = channels >= 4 ? pixels[source + 3] : 255;
        }
        return rgba;
    }

    std::optional<DecodedImage> DecodeImageFile(const std::string &path)
    {
        if (path.empty())
            return std::nullopt;
        std::string bytes;
        if (!content::ReadFile(path, bytes))
            return std::nullopt;
        DecodedImage image;
        if (IsDds(path))
        {
            DdsImage dds;
            if (!DecodeDds({reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()}, dds) ||
                !IsTextureSizeSupported(dds.width, dds.height))
                return std::nullopt;
            image.width = dds.width;
            image.height = dds.height;
            image.channels = 4;
            image.rgba8.assign(reinterpret_cast<const unsigned char *>(dds.pixels.data()),
                               reinterpret_cast<const unsigned char *>(dds.pixels.data()) + dds.pixels.size());
            return image;
        }
        if (bytes.size() > INT_MAX)
            return std::nullopt;
        int width = 0, height = 0, channels = 0;
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()), static_cast<int>(bytes.size()), &width, &height,
                                  &channels, 0),
            &stbi_image_free);
        if (!pixels || channels < 1 || channels > 4 || !IsTextureSizeSupported(width, height))
            return std::nullopt;
        image.width = width;
        image.height = height;
        image.channels = channels;
        image.rgba8 = ExpandPixelsToRgba8(pixels.get(), width, height, channels);
        return image;
    }
}
