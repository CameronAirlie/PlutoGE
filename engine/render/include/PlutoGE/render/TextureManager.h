#pragma once

#include "PlutoGE/render/Texture.h"

#include <unordered_map>
#include <string>
#include <glad/glad.h>

namespace PlutoGE::platform
{
    class Window;
}

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

        void SetWindow(platform::Window *window) { m_window = window; }

        Texture *FindTexture(const std::string &cacheKey) const;
        // Owner-thread refresh of cached ordinary image files. Preserve object
        // identity/borrowers; publish both colour spaces only after stable input
        // verification. Failure retains previous pixels and GPU allocations.
        bool ReloadFileTexture(const std::string &filePath, std::string *errorMessage = nullptr);
        Texture *LoadTextureFromFile(const char *filePath, TextureColorSpace colorSpace = TextureColorSpace::Linear);
        Texture *LoadTextureFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels, TextureColorSpace colorSpace = TextureColorSpace::Linear);
        Texture *LoadEnvironmentTextureFromFile(const char *filePath);
        Texture *LoadLightmapFromFile(const char *filePath);
        Texture *LoadLightmapFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels);
        Texture *LoadLightmapFromMemory(const std::string &cacheKey, const float *pixels, int width, int height, int channels);

        Texture *CreateDepthTexture(int width, int height);
        Texture *CreateDepthCubemap(int width, int height);
        Texture *CreateColorCubemap(int width, int height);

    private:
        bool PrepareForGpuAccess() const;
        // Render textures share one instance per asset, whatever colour space
        // the requesting material slot asks for.
        Texture *LoadRenderTexture(const std::string &assetPath);

        GLuint m_nextTextureID = 1;                                // Start from 1 since 0 is reserved for "no texture"
        std::unordered_map<std::string, Texture *> m_textureCache; // Cache for loaded textures
        // Keep newly added state after the established cache fields. Besides making the layout easier
        // to evolve, this preserves their offsets when an incremental MSVC build contains an older
        // object file compiled before window-aware texture uploads were introduced.
        platform::Window *m_window = nullptr;
        bool m_ownResources = false;
    };
}
