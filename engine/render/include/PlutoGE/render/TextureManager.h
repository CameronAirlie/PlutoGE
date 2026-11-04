#pragma once

#include "PlutoGE/render/Texture.h"

#include <unordered_map>
#include <string>

namespace PlutoGE::render
{
    class Texture;
    class TextureManager
    {
    public:
        explicit TextureManager(bool ownResources = false) : m_ownResources(ownResources) {}
        ~TextureManager();
        TextureManager(const TextureManager &) = delete;
        TextureManager &operator=(const TextureManager &) = delete;

        Texture *FindTexture(const std::string &cacheKey) const;
        // Owner-thread refresh of cached ordinary image files. Preserve object
        // identity/borrowers; publish both colour spaces only after stable input
        // verification. Failure retains the previous pixels.
        bool ReloadFileTexture(const std::string &filePath, std::string *errorMessage = nullptr);
        Texture *LoadTextureFromFile(const char *filePath, TextureColorSpace colorSpace = TextureColorSpace::Linear);
        Texture *LoadTextureFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels);
        Texture *LoadEnvironmentTextureFromFile(const char *filePath);

    private:
        // Render textures share one instance per asset, whatever colour space
        // the requesting material slot asks for.
        Texture *LoadRenderTexture(const std::string &assetPath);

        std::unordered_map<std::string, Texture *> m_textureCache;
        bool m_ownResources = false;
    };
}
