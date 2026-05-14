#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/Texture.h"
#include <glad/glad.h>

#include <algorithm>
#include <fstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace PlutoGE::render
{
    namespace
    {
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

            const std::size_t pixelCount = static_cast<std::size_t>(outImage.width * outImage.height * outImage.channels);
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
    }

    Texture *TextureManager::LoadTextureFromFile(const char *filePath)
    {
        // Check if the texture is already loaded
        auto it = m_textureCache.find(filePath);
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
            TextureConfig config;
            config.filePath = filePath;
            Texture *texture = new Texture(config);

            glGenTextures(1, &texture->m_textureID);
            glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
            ConfigureTexture2D(GL_REPEAT, true);
            const GLenum format = ResolveTextureFormat(channels);
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            texture->m_width = width;
            texture->m_height = height;
            texture->m_channels = channels;

            stbi_image_free(data);
            if (!texture)
            {
                return nullptr;
            }

            m_textureCache[filePath] = texture;
            return texture;
        }

        return nullptr; // Failed to load texture
    }

    Texture *TextureManager::LoadTextureFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels)
    {
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!pixels || width <= 0 || height <= 0 || channels <= 0)
        {
            return nullptr;
        }

        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureTexture2D(GL_REPEAT, true);
        const GLenum format = ResolveTextureFormat(channels);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::LoadLightmapFromFile(const char *filePath)
    {
        auto it = m_textureCache.find(filePath);
        if (it != m_textureCache.end())
        {
            return it->second;
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

            const GLenum format = ResolveTextureFormat(pfmImage.channels);
            const GLenum internalFormat = pfmImage.channels >= 4 ? GL_RGBA16F : (pfmImage.channels == 1 ? GL_R16F : GL_RGB16F);
            TextureConfig config;
            config.filePath = lightmapPath;
            Texture *texture = new Texture(config);

            glGenTextures(1, &texture->m_textureID);
            glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
            ConfigureTexture2D(GL_CLAMP_TO_EDGE, true);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, pfmImage.width, pfmImage.height, 0, format, GL_FLOAT, pfmImage.pixels.data());
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

        TextureConfig config;
        config.filePath = filePath;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureTexture2D(GL_CLAMP_TO_EDGE, true);
        const GLenum format = ResolveTextureFormat(channels);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
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

        if (!pixels || width <= 0 || height <= 0 || channels <= 0)
        {
            return nullptr;
        }

        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureTexture2D(GL_CLAMP_TO_EDGE, true);
        const GLenum format = ResolveTextureFormat(channels);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::LoadLightmapFromMemory(const std::string &cacheKey, const float *pixels, int width, int height, int channels)
    {
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!pixels || width <= 0 || height <= 0 || channels <= 0)
        {
            return nullptr;
        }

        const GLenum format = ResolveTextureFormat(channels);
        const GLenum internalFormat = channels >= 4 ? GL_RGBA16F : (channels == 1 ? GL_R16F : GL_RGB16F);
        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
        ConfigureTexture2D(GL_CLAMP_TO_EDGE, true);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_FLOAT, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::CreateDepthTexture(int width, int height)
    {
        TextureConfig config;
        Texture *texture = new Texture(config);
        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
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
        TextureConfig config;
        Texture *texture = new Texture(config);
        texture->m_type = GL_TEXTURE_CUBE_MAP;

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture->m_textureID);
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
}