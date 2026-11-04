#include <climits>
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/platform/ContentDigest.h"
#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/DdsImage.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include <stb_image.h>

namespace PlutoGE::render
{
    namespace
    {
        unsigned char *LoadImage(const char *path, int *w, int *h, int *channels, int desired)
        {
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (extension == ".dds")
            {
                std::string bytes;
                if (!content::ReadFile(path, bytes)) return nullptr;
                DdsImage image;
                if (!DecodeDds({reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()}, image)) return nullptr;
                if (desired != 0 && desired != 4) return nullptr;
                // Callers release these with stbi_image_free, which uses the default STBI_FREE (free).
                auto *pixels = static_cast<unsigned char *>(std::malloc(image.pixels.size()));
                if (!pixels) return nullptr;
                std::memcpy(pixels, image.pixels.data(), image.pixels.size());
                *w = image.width; *h = image.height; *channels = 4;
                return pixels;
            }
            if (!content::IsMounted(path)) return stbi_load(path, w, h, channels, desired);
            std::string bytes;
            if (!content::ReadFile(path, bytes) || bytes.size() > INT_MAX) return nullptr;
            return stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()), static_cast<int>(bytes.size()), w, h, channels, desired);
        }
        float *LoadFloatImage(const char *path, int *w, int *h, int *channels, int desired)
        {
            if (!content::IsMounted(path)) return stbi_loadf(path, w, h, channels, desired);
            std::string bytes;
            if (!content::ReadFile(path, bytes) || bytes.size() > INT_MAX) return nullptr;
            return stbi_loadf_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()), static_cast<int>(bytes.size()), w, h, channels, desired);
        }
        bool IsHdrImage(const char *path)
        {
            if (!content::IsMounted(path)) return stbi_is_hdr(path) != 0;
            std::string bytes;
            if (!content::ReadFile(path, bytes) || bytes.size() > INT_MAX) return false;
            return stbi_is_hdr_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()), static_cast<int>(bytes.size())) != 0;
        }

        struct PfmImageData
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            std::vector<float> pixels;
        };

        bool LoadPfm(const char *filePath, PfmImageData &outImage)
        {
            PlutoGE::content::InputFile input(filePath, std::ios::binary);
            if (!input.is_open())
            {
                return false;
            }

            std::string header;
            input >> header;
            if (header != "PF" && header != "Pf")
            {
                return false;
            }

            outImage.channels = header == "PF" ? 3 : 1;
            input >> outImage.width >> outImage.height;
            float scale = 0.0f;
            input >> scale;
            input.get();

            if (outImage.width <= 0 || outImage.height <= 0 || scale >= 0.0f)
            {
                return false;
            }

            const std::size_t width = static_cast<std::size_t>(outImage.width);
            const std::size_t height = static_cast<std::size_t>(outImage.height);
            const std::size_t channels = static_cast<std::size_t>(outImage.channels);
            if (width > std::numeric_limits<std::size_t>::max() / height ||
                width * height > std::numeric_limits<std::size_t>::max() / channels)
            {
                outImage = {};
                return false;
            }

            const std::size_t pixelCount = width * height * channels;
            outImage.pixels.resize(pixelCount);
            input.read(reinterpret_cast<char *>(outImage.pixels.data()), static_cast<std::streamsize>(pixelCount * sizeof(float)));
            if (!input)
            {
                outImage = {};
                return false;
            }

            std::vector<float> flippedPixels(pixelCount);
            const std::size_t rowStride = static_cast<std::size_t>(outImage.width * outImage.channels);
            for (int row = 0; row < outImage.height; ++row)
            {
                const std::size_t srcOffset = static_cast<std::size_t>((outImage.height - 1 - row)) * rowStride;
                const std::size_t dstOffset = static_cast<std::size_t>(row) * rowStride;
                std::copy_n(outImage.pixels.data() + srcOffset, rowStride, flippedPixels.data() + dstOffset);
            }

            outImage.pixels.swap(flippedPixels);
            return true;
        }

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
            return value;
        }

        const char *TextureColorSpaceSuffix(TextureColorSpace colorSpace)
        {
            return colorSpace == TextureColorSpace::SRGB ? "#srgb" : "#linear";
        }

        std::string BuildTextureCacheKey(std::string_view baseKey, TextureColorSpace colorSpace)
        {
            std::string cacheKey(baseKey);
            cacheKey += TextureColorSpaceSuffix(colorSpace);
            return cacheKey;
        }

        std::vector<unsigned char> ExpandBytePixelsToRgba(const unsigned char *pixels, int width, int height, int channels)
        {
            const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            std::vector<unsigned char> rgbaPixels(pixelCount * 4, 255);
            for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                const std::size_t srcOffset = pixelIndex * static_cast<std::size_t>(channels);
                const std::size_t dstOffset = pixelIndex * 4;
                rgbaPixels[dstOffset] = pixels[srcOffset];
                rgbaPixels[dstOffset + 1] = channels >= 2 ? pixels[srcOffset + 1] : pixels[srcOffset];
                rgbaPixels[dstOffset + 2] = channels >= 3 ? pixels[srcOffset + 2] : pixels[srcOffset];
                rgbaPixels[dstOffset + 3] = channels >= 4 ? pixels[srcOffset + 3] : 255;
            }
            return rgbaPixels;
        }

        bool IsTextureSizeSupported(int width, int height)
        {
            // The largest 2D extent both RHI backends accept.
            constexpr int kMaxTextureExtent = 16384;
            return width > 0 && height > 0 && width <= kMaxTextureExtent && height <= kMaxTextureExtent;
        }
    }

    TextureManager::~TextureManager()
    {
        if (!m_ownResources) return;
        for (const auto &[key, texture] : m_textureCache)
            delete texture;
    }

    Texture *TextureManager::LoadRenderTexture(const std::string &assetPath)
    {
        if (core::Engine::GetInstance().GetConfig().headless) return nullptr;
        // Materials, cameras and UI documents spell the same asset differently
        // (normalized asset paths vs. canonical document-relative paths).
        std::error_code canonicalError;
        auto canonicalPath = std::filesystem::weakly_canonical(assetPath, canonicalError);
        if (canonicalError)
            canonicalPath = std::filesystem::path(assetPath).lexically_normal();
        const std::string cacheKey = canonicalPath.generic_string() + "#render";
        if (const auto cached = m_textureCache.find(cacheKey); cached != m_textureCache.end())
            return cached->second;
        const auto descriptor = RenderTexture::LoadDescriptor(assetPath);
        if (!descriptor)
            return nullptr;
        auto *texture = new RenderTexture(assetPath, *descriptor);
        m_textureCache.emplace(cacheKey, texture);
        return texture;
    }

    Texture *TextureManager::FindTexture(const std::string &cacheKey) const
    {
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        return nullptr;
    }

    bool TextureManager::ReloadFileTexture(const std::string &filePath, std::string *error)
    {
        const auto fail = [&](const char *message) { if (error) *error = message; return false; };
        if (filePath.empty() || RenderTexture::IsAssetPath(filePath))
            return fail("Ordinary image refresh requires a file texture.");
        std::vector<std::pair<Texture *, TextureColorSpace>> cached;
        for (const auto colorSpace : {TextureColorSpace::Linear, TextureColorSpace::SRGB})
            if (const auto found = m_textureCache.find(BuildTextureCacheKey(filePath, colorSpace)); found != m_textureCache.end())
                cached.emplace_back(found->second, colorSpace);
        if (cached.empty()) { if (error) error->clear(); return true; }
        const auto hash = [&](content::ContentDigest &digest)
        {
            if (!content::IsMounted(filePath)) return content::HashFileContent(filePath, digest, error);
            std::string bytes;
            if (!content::ReadFile(filePath, bytes)) return fail("Cannot read packed image for refresh.");
            digest = content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())));
            return true;
        };
        content::ContentDigest before, after;
        if (!hash(before)) return false;
        TextureManager staging(true);
        std::vector<std::pair<Texture *, Texture *>> replacements;
        for (const auto &[texture, colorSpace] : cached)
        {
            auto *replacement = staging.LoadTextureFromFile(filePath.c_str(), colorSpace);
            if (!replacement)
                return fail("Changed image could not be decoded; cached pixels were retained.");
            replacements.emplace_back(texture, replacement);
        }
        if (!hash(after)) return false;
        if (before != after) return fail("Image changed while preparing its refresh; cached pixels were retained.");
        // Renderers see the stable object's new revision and upload it again.
        for (const auto &[texture, replacement] : replacements)
        {
            std::swap(texture->m_width, replacement->m_width);
            std::swap(texture->m_height, replacement->m_height);
            std::swap(texture->m_channels, replacement->m_channels);
            texture->m_rgba8Pixels.swap(replacement->m_rgba8Pixels);
            ++texture->m_contentRevision;
        }
        if (error) error->clear();
        return true;
    }

    Texture *TextureManager::LoadTextureFromFile(const char *filePath, TextureColorSpace colorSpace)
    {
        if (core::Engine::GetInstance().GetConfig().headless) return nullptr;
        if (!filePath || filePath[0] == '\0')
        {
            return nullptr;
        }

        const std::string sourcePath(filePath);
        if (RenderTexture::IsAssetPath(sourcePath))
            return LoadRenderTexture(sourcePath);
        const std::string cacheKey = BuildTextureCacheKey(sourcePath, colorSpace);

        // Check if the texture is already loaded
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        int width, height, channels;
        unsigned char *data = LoadImage(filePath, &width, &height, &channels, 0);
        if (!data)
        {
            return nullptr;
        }

        if (channels < 1 || channels > 4 || !IsTextureSizeSupported(width, height))
        {
            stbi_image_free(data);
            return nullptr;
        }

        TextureConfig config;
        config.filePath = filePath;
        Texture *texture = new Texture(config);
        texture->m_rgba8Pixels = ExpandBytePixelsToRgba(data, width, height, channels);
        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;
        stbi_image_free(data);

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::LoadTextureFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels)
    {
        if (core::Engine::GetInstance().GetConfig().headless) return nullptr;
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!pixels || channels < 1 || channels > 4 || !IsTextureSizeSupported(width, height))
        {
            return nullptr;
        }

        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);
        texture->m_rgba8Pixels = ExpandBytePixelsToRgba(pixels, width, height, channels);
        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::LoadEnvironmentTextureFromFile(const char *filePath)
    {
        if (core::Engine::GetInstance().GetConfig().headless) return nullptr;
        if (!filePath || filePath[0] == '\0')
        {
            return nullptr;
        }

        auto it = m_textureCache.find(filePath);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        const std::string environmentPath(filePath);
        const std::string lowerPath = ToLower(environmentPath);
        const bool isPfm = lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".pfm") == 0;
        const bool isHdr = lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".hdr") == 0;

        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> rgba8Pixels;
        if (isPfm)
        {
            PfmImageData pfmImage;
            if (!LoadPfm(filePath, pfmImage))
            {
                return nullptr;
            }

            width = pfmImage.width;
            height = pfmImage.height;
            channels = pfmImage.channels;
        }
        else if (isHdr || IsHdrImage(filePath))
        {
            float *data = LoadFloatImage(filePath, &width, &height, &channels, 0);
            if (!data)
            {
                return nullptr;
            }
            stbi_image_free(data);
        }
        else
        {
            unsigned char *data = LoadImage(filePath, &width, &height, &channels, 0);
            if (!data)
            {
                return nullptr;
            }
            if (channels >= 1 && channels <= 4)
                rgba8Pixels = ExpandBytePixelsToRgba(data, width, height, channels);
            stbi_image_free(data);
        }

        if (channels < 1 || channels > 4 || !IsTextureSizeSupported(width, height))
        {
            return nullptr;
        }

        // Floating-point maps are decoded to validate them, but no renderer
        // samples their texels, so only their size is kept.
        TextureConfig config;
        config.filePath = environmentPath;
        Texture *texture = new Texture(config);
        texture->m_rgba8Pixels = std::move(rgba8Pixels);
        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[environmentPath] = texture;
        return texture;
    }
}
