#pragma once

#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/TextureDecodeQueue.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace PlutoGE::render
{
    // What happens to a file texture's CPU pixels once the renderers hold its GPU copies (Texture::MarkUploaded).
    enum class TexturePixelRetention : std::uint8_t
    {
        // Every texture keeps its pixels: editors read them for thumbnails, several renderers and hot reload.
        Keep,
        // File textures drop them (ReleaseUploadedPixels) and decode their file again if something reads them later.
        // A game's CPU copies otherwise duplicate every texture it has uploaded.
        ReleaseAfterUpload,
    };

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
        // Starts decoding image files that are about to be loaded (a material's textures) on worker threads, so they
        // decode side by side; LoadTextureFromFile then takes each result. Files already loaded are skipped.
        void PrefetchTextureFiles(std::span<const std::string> filePaths);
        // Decodes off the owner thread: a prefetched decode when there is one, else through LoadingWork (so a loading
        // screen keeps presenting). Returns the complete texture, as before.
        Texture *LoadTextureFromFile(const char *filePath, TextureColorSpace colorSpace = TextureColorSpace::Linear);
        Texture *LoadTextureFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels);
        Texture *LoadEnvironmentTextureFromFile(const char *filePath);

        void SetPixelRetention(TexturePixelRetention retention) noexcept { m_pixelRetention = retention; }
        [[nodiscard]] TexturePixelRetention GetPixelRetention() const noexcept { return m_pixelRetention; }
        // Owner thread, between frames: under ReleaseAfterUpload, drops the CPU pixels of every uploaded file texture.
        // Returns the bytes released.
        std::size_t ReleaseUploadedPixels();

    private:
        // Render textures share one instance per asset, whatever colour space
        // the requesting material slot asks for.
        Texture *LoadRenderTexture(const std::string &assetPath);

        std::unordered_map<std::string, Texture *> m_textureCache;
        TextureDecodeQueue m_decodes;
        TexturePixelRetention m_pixelRetention = TexturePixelRetention::Keep;
        bool m_ownResources = false;
    };
}
