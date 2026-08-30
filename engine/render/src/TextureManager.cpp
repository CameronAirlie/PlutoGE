#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/platform/Window.h"
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kMaxHalfFloatValue = 65504.0f;

        GLenum ResolveTextureFormat(int channels)
        {
            switch (channels)
            {
            case 1:
                return GL_RED;
            case 2:
                return GL_RG;
            case 3:
                return GL_RGB;
            case 4:
                return GL_RGBA;
            default:
                return GL_RGBA;
            }
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
            std::ifstream input(filePath, std::ios::binary);
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

        void ConfigureTexture2D(GLenum wrapMode, bool generateMipmaps)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        void ConfigureLightmapTexture2D()
        {
            // Baked atlases are dilated by sixteen texels. Retain enough mips
            // for stable minification on large floors and grazing surfaces, but
            // stop before filters can span beyond that gutter into unrelated UV
            // islands.
            ConfigureTexture2D(GL_CLAMP_TO_EDGE, true);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
        }

        GLenum ResolveFloatTextureInternalFormat(int channels)
        {
            if (channels >= 4)
            {
                return GL_RGBA16F;
            }

            if (channels == 1)
            {
                return GL_R16F;
            }

            return GL_RGB16F;
        }

        std::vector<float> ExpandFloatPixelsToRgba(const float *pixels, int width, int height, int channels)
        {
            const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            std::vector<float> rgbaPixels(pixelCount * 4, 1.0f);
            for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                const std::size_t srcOffset = pixelIndex * static_cast<std::size_t>(channels);
                const std::size_t dstOffset = pixelIndex * 4;

                if (channels >= 1)
                {
                    rgbaPixels[dstOffset] = pixels[srcOffset];
                    rgbaPixels[dstOffset + 1] = pixels[srcOffset];
                    rgbaPixels[dstOffset + 2] = pixels[srcOffset];
                }

                if (channels >= 2)
                {
                    rgbaPixels[dstOffset + 1] = pixels[srcOffset + 1];
                }

                if (channels >= 3)
                {
                    rgbaPixels[dstOffset + 2] = pixels[srcOffset + 2];
                }

                if (channels >= 4)
                {
                    rgbaPixels[dstOffset + 3] = pixels[srcOffset + 3];
                }
            }

            return rgbaPixels;
        }

        void SanitizeFloatPixelsForHalfFloatUpload(std::vector<float> &pixels)
        {
            for (float &component : pixels)
            {
                if (!std::isfinite(component) || component < 0.0f)
                {
                    component = 0.0f;
                    continue;
                }

                component = std::min(component, kMaxHalfFloatValue);
            }
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

        void ConfigureEnvironmentTexture2D(bool generateMipmaps)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        GLint ResolveByteTextureInternalFormat(int channels)
        {
            switch (channels)
            {
            case 1:
                return GL_R8;
            case 2:
                return GL_RG8;
            case 3:
                return GL_RGB8;
            case 4:
                return GL_RGBA8;
            default:
                return GL_RGBA8;
            }
        }

        GLint ResolveByteTextureInternalFormat(int channels, TextureColorSpace colorSpace)
        {
            if (colorSpace == TextureColorSpace::SRGB)
            {
                switch (channels)
                {
                case 3:
                    return GL_SRGB8;
                case 4:
                    return GL_SRGB8_ALPHA8;
                default:
                    break;
                }
            }

            return ResolveByteTextureInternalFormat(channels);
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
            if (width <= 0 || height <= 0)
            {
                return false;
            }

            if (!glad_glGetIntegerv)
                return width <= 16384 && height <= 16384;
            GLint maximumTextureSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
            return maximumTextureSize > 0 && width <= maximumTextureSize && height <= maximumTextureSize;
        }

        void UploadTexture2D(GLint internalFormat, int width, int height, GLenum format, GLenum type, const void *pixels)
        {
            // A CPU pointer is interpreted as a byte offset whenever a pixel-unpack buffer is bound.
            // Scene loading can happen between editor render passes, so never inherit that state from
            // another uploader. Reset every unpack stride/offset that changes how the driver reads the
            // supplied allocation before entering glTexImage2D.
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);

            GLsizei mipLevels = 1;
            for (int largestDimension = std::max(width, height); largestDimension > 1; largestDimension /= 2)
            {
                ++mipLevels;
            }

            // Keep allocation separate from the client-memory transfer. Besides requiring an explicit
            // storage format, this avoids the AMD glTexImage2D path that was crashing during scene load.
            glTexStorage2D(GL_TEXTURE_2D, mipLevels, static_cast<GLenum>(internalFormat), width, height);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, pixels);
        }
    }

    bool TextureManager::PrepareForGpuAccess() const
    {
        if (!m_window || !m_window->IsOpen())
        {
            return false;
        }

        // File and scene resource uploads can occur between editor render passes,
        // after a platform viewport used a different context. Refresh dispatch for
        // the restored engine context before issuing allocation calls.
        return m_window->EnsureOpenGLContextCurrent(true);
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

    Texture *TextureManager::LoadTextureFromFile(const char *filePath, TextureColorSpace colorSpace)
    {
        if (!filePath || filePath[0] == '\0')
        {
            return nullptr;
        }

        const std::string sourcePath(filePath);
        const std::string cacheKey = BuildTextureCacheKey(sourcePath, colorSpace);

        // Check if the texture is already loaded
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        // Load the texture
        // Load image data
        int width, height, channels;
        unsigned char *data = stbi_load(filePath, &width, &height, &channels, 0);
        if (data)
        {
            if (channels < 1 || channels > 4 || !IsTextureSizeSupported(width, height))
            {
                stbi_image_free(data);
                return nullptr;
            }

            TextureConfig config;
            config.filePath = filePath;
            Texture *texture = new Texture(config);
            texture->m_rgba8Pixels = ExpandBytePixelsToRgba(data, width, height, channels);
            // RGB file textures (normal maps in particular) use a driver-sensitive three-byte row
            // layout. Upload an explicit RGBA8 allocation instead; alpha is opaque and RGB is exact.
            std::vector<unsigned char> expandedPixels;
            const unsigned char *uploadPixels = data;
            int uploadChannels = channels;
            if (channels == 3)
            {
                expandedPixels = texture->m_rgba8Pixels;
                uploadPixels = expandedPixels.data();
                uploadChannels = 4;
            }
            if (PrepareForGpuAccess())
            {
                glGenTextures(1, &texture->m_textureID);
                Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
                ConfigureTexture2D(GL_REPEAT, true);
                const GLenum format = ResolveTextureFormat(uploadChannels);
                UploadTexture2D(ResolveByteTextureInternalFormat(uploadChannels, colorSpace), width, height, format, GL_UNSIGNED_BYTE, uploadPixels);
                glGenerateMipmap(GL_TEXTURE_2D);
            }

            texture->m_width = width;
            texture->m_height = height;
            texture->m_channels = uploadChannels;

            stbi_image_free(data);
            if (!texture)
            {
                return nullptr;
            }

            m_textureCache[cacheKey] = texture;
            return texture;
        }

        return nullptr; // Failed to load texture
    }

    Texture *TextureManager::LoadTextureFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels, TextureColorSpace colorSpace)
    {
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!pixels || channels < 1 || channels > 4)
        {
            return nullptr;
        }

        if (!IsTextureSizeSupported(width, height))
        {
            return nullptr;
        }

        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);

        texture->m_rgba8Pixels = ExpandBytePixelsToRgba(pixels, width, height, channels);
        if (PrepareForGpuAccess())
        {
            glGenTextures(1, &texture->m_textureID);
            Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
            ConfigureTexture2D(GL_REPEAT, true);
            const GLenum format = ResolveTextureFormat(channels);
            UploadTexture2D(ResolveByteTextureInternalFormat(channels, colorSpace), width, height, format, GL_UNSIGNED_BYTE, pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::LoadEnvironmentTextureFromFile(const char *filePath)
    {
        if (!filePath || filePath[0] == '\0')
        {
            return nullptr;
        }

        auto it = m_textureCache.find(filePath);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!PrepareForGpuAccess())
        {
            return nullptr;
        }

        const std::string environmentPath(filePath);
        const std::string lowerPath = ToLower(environmentPath);
        const bool isPfm = lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".pfm") == 0;
        const bool isHdr = lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".hdr") == 0;

        if (isPfm)
        {
            PfmImageData pfmImage;
            if (!LoadPfm(filePath, pfmImage))
            {
                return nullptr;
            }

            if (!IsTextureSizeSupported(pfmImage.width, pfmImage.height))
            {
                return nullptr;
            }

            SanitizeFloatPixelsForHalfFloatUpload(pfmImage.pixels);

            const bool useRgbaUpload = pfmImage.channels > 1 && pfmImage.channels < 4;
            const GLenum format = useRgbaUpload ? GL_RGBA : ResolveTextureFormat(pfmImage.channels);
            const GLenum internalFormat = useRgbaUpload ? GL_RGBA16F : ResolveFloatTextureInternalFormat(pfmImage.channels);
            std::vector<float> expandedPixels;
            const float *uploadPixels = pfmImage.pixels.data();
            if (useRgbaUpload)
            {
                expandedPixels = ExpandFloatPixelsToRgba(pfmImage.pixels.data(), pfmImage.width, pfmImage.height, pfmImage.channels);
                uploadPixels = expandedPixels.data();
            }
            TextureConfig config;
            config.filePath = environmentPath;
            Texture *texture = new Texture(config);

            glGenTextures(1, &texture->m_textureID);
            Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
            ConfigureEnvironmentTexture2D(true);
            UploadTexture2D(static_cast<GLint>(internalFormat), pfmImage.width, pfmImage.height, format, GL_FLOAT, uploadPixels);
            glGenerateMipmap(GL_TEXTURE_2D);

            texture->m_width = pfmImage.width;
            texture->m_height = pfmImage.height;
            texture->m_channels = pfmImage.channels;

            m_textureCache[environmentPath] = texture;
            return texture;
        }

        if (isHdr || stbi_is_hdr(filePath))
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            float *data = stbi_loadf(filePath, &width, &height, &channels, 0);
            if (!data)
            {
                return nullptr;
            }

            if (channels < 1 || channels > 4 || !IsTextureSizeSupported(width, height))
            {
                stbi_image_free(data);
                return nullptr;
            }

            const bool useRgbaUpload = channels > 1 && channels < 4;
            const GLenum format = useRgbaUpload ? GL_RGBA : ResolveTextureFormat(channels);
            const GLenum internalFormat = useRgbaUpload ? GL_RGBA16F : ResolveFloatTextureInternalFormat(channels);
            std::vector<float> sanitizedPixels(data, data + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels));
            SanitizeFloatPixelsForHalfFloatUpload(sanitizedPixels);

            std::vector<float> expandedPixels;
            const float *uploadPixels = sanitizedPixels.data();
            if (useRgbaUpload)
            {
                expandedPixels = ExpandFloatPixelsToRgba(sanitizedPixels.data(), width, height, channels);
                uploadPixels = expandedPixels.data();
            }
            TextureConfig config;
            config.filePath = environmentPath;
            Texture *texture = new Texture(config);

            glGenTextures(1, &texture->m_textureID);
            Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
            ConfigureEnvironmentTexture2D(true);
            UploadTexture2D(static_cast<GLint>(internalFormat), width, height, format, GL_FLOAT, uploadPixels);
            glGenerateMipmap(GL_TEXTURE_2D);

            texture->m_width = width;
            texture->m_height = height;
            texture->m_channels = channels;

            stbi_image_free(data);

            m_textureCache[environmentPath] = texture;
            return texture;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char *data = stbi_load(filePath, &width, &height, &channels, 0);
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
        config.filePath = environmentPath;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureEnvironmentTexture2D(true);
        const GLenum format = ResolveTextureFormat(channels);
        UploadTexture2D(ResolveByteTextureInternalFormat(channels), width, height, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        stbi_image_free(data);

        m_textureCache[environmentPath] = texture;
        return texture;
    }

    Texture *TextureManager::LoadLightmapFromFile(const char *filePath)
    {
        if (!filePath || filePath[0] == '\0')
        {
            return nullptr;
        }

        auto it = m_textureCache.find(filePath);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!PrepareForGpuAccess())
        {
            return nullptr;
        }

        const std::string lightmapPath(filePath ? filePath : "");
        const bool isPfm = lightmapPath.size() >= 4 && lightmapPath.compare(lightmapPath.size() - 4, 4, ".pfm") == 0;
        if (isPfm)
        {
            PfmImageData pfmImage;
            if (!LoadPfm(filePath, pfmImage))
            {
                return nullptr;
            }

            if (!IsTextureSizeSupported(pfmImage.width, pfmImage.height))
            {
                return nullptr;
            }

            const GLenum format = ResolveTextureFormat(pfmImage.channels);
            const GLenum internalFormat = pfmImage.channels >= 4 ? GL_RGBA16F : (pfmImage.channels == 1 ? GL_R16F : GL_RGB16F);
            TextureConfig config;
            config.filePath = lightmapPath;
            Texture *texture = new Texture(config);

            glGenTextures(1, &texture->m_textureID);
            Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
            ConfigureLightmapTexture2D();
            UploadTexture2D(static_cast<GLint>(internalFormat), pfmImage.width, pfmImage.height, format, GL_FLOAT, pfmImage.pixels.data());
            glGenerateMipmap(GL_TEXTURE_2D);

            texture->m_width = pfmImage.width;
            texture->m_height = pfmImage.height;
            texture->m_channels = pfmImage.channels;

            m_textureCache[lightmapPath] = texture;
            return texture;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char *data = stbi_load(filePath, &width, &height, &channels, 0);
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

        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureLightmapTexture2D();
        const GLenum format = ResolveTextureFormat(channels);
        UploadTexture2D(ResolveByteTextureInternalFormat(channels), width, height, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        stbi_image_free(data);

        m_textureCache[filePath] = texture;
        return texture;
    }

    Texture *TextureManager::LoadLightmapFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels)
    {
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!pixels || channels < 1 || channels > 4)
        {
            return nullptr;
        }

        if (!PrepareForGpuAccess())
        {
            return nullptr;
        }

        if (!IsTextureSizeSupported(width, height))
        {
            return nullptr;
        }

        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureLightmapTexture2D();
        const GLenum format = ResolveTextureFormat(channels);
        UploadTexture2D(ResolveByteTextureInternalFormat(channels), width, height, format, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::LoadLightmapFromMemory(const std::string &cacheKey, const float *pixels, int width, int height, int channels)
    {
        if (!pixels || channels < 1 || channels > 4)
        {
            return nullptr;
        }

        if (!PrepareForGpuAccess())
        {
            return nullptr;
        }

        if (!IsTextureSizeSupported(width, height))
        {
            return nullptr;
        }

        const GLenum format = ResolveTextureFormat(channels);
        const GLenum internalFormat = ResolveFloatTextureInternalFormat(channels);

        // A scene bake deliberately reuses its stable output path. Returning the
        // cached object here used to leave the old GPU pixels in place, making
        // every bake after the first appear to have done nothing. Preserve the
        // Texture pointer held by materials, but replace its storage in-place.
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            Texture *texture = it->second;
            if (!texture || texture->m_type != GL_TEXTURE_2D || texture->m_textureID == 0)
            {
                return nullptr;
            }

            Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
            ConfigureLightmapTexture2D();
            UploadTexture2D(static_cast<GLint>(internalFormat), width, height, format, GL_FLOAT, pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            texture->m_width = width;
            texture->m_height = height;
            texture->m_channels = channels;
            return texture;
        }

        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureLightmapTexture2D();
        UploadTexture2D(static_cast<GLint>(internalFormat), width, height, format, GL_FLOAT, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::CreateDepthTexture(int width, int height)
    {
        if (width <= 0 || height <= 0 || !PrepareForGpuAccess())
        {
            return nullptr;
        }

        TextureConfig config;
        Texture *texture = new Texture(config);
        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = 1; // Depth textures have 1 channel

        return texture;
    }

    Texture *TextureManager::CreateDepthCubemap(int width, int height)
    {
        if (width <= 0 || height <= 0 || !PrepareForGpuAccess())
        {
            return nullptr;
        }

        TextureConfig config;
        Texture *texture = new Texture(config);
        texture->m_type = GL_TEXTURE_CUBE_MAP;

        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, texture->m_textureID);
        for (unsigned int face = 0; face < 6; ++face)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = 1;

        return texture;
    }

    Texture *TextureManager::CreateColorCubemap(int width, int height)
    {
        if (width <= 0 || height <= 0 || !PrepareForGpuAccess())
        {
            return nullptr;
        }

        TextureConfig config;
        Texture *texture = new Texture(config);
        texture->m_type = GL_TEXTURE_CUBE_MAP;

        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, texture->m_textureID);
        for (unsigned int face = 0; face < 6; ++face)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, 0);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = 4;
        return texture;
    }
}
