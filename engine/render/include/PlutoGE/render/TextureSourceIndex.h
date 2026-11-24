#pragma once

#include "PlutoGE/platform/ContentDigest.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/TextureCompression.h"

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// What became of source image files, so loading a texture whose compressed mip chains already exist needs no decode.
//
// The renderer compresses material textures and keeps the results in a store keyed by their texels
// (CompressedTextureStore), so finding an entry used to require decoding the image first. The index maps a source file
// (as it is now) to its entries, its size and its mean colour:
//  - TextureManager makes such a texture from the record alone: its size is known, and its pixels decode only when
//    CPU code reads them (Texture's pixel source).
//  - The renderer uploads the stored chain (RhiTextureCompression::LoadIndexed) instead of reading pixels.
// Renderers record what they compress (the editor and development runs fill it as they go); an exported game ships
// a read-only index its cook wrote (TextureCooking.h), so players never decode material textures at all.
namespace PlutoGE::render
{
    // A texture's mean texel over a sample of its texels: raw, and with colour decoded from sRGB (alpha as stored).
    // Renderers use it where they do not sample the texture.
    struct TextureMeans
    {
        glm::vec4 raw{0.0f}, srgb{0.0f};
    };
    [[nodiscard]] TextureMeans ComputeTextureMeans(std::span<const std::byte> rgba8);

    // Everything besides the texels that decides what SelectTextureCompression picks and what the encoders produce:
    // a recorded format is only reused under the same policy.
    [[nodiscard]] std::uint64_t TextureCompressionPolicy(const TextureCompressionSettings &settings,
                                                         const SampledFormatQuery &supports);

    // One way a source image is sampled, and the store entry holding its compressed mip chain.
    struct TextureSourceSampling
    {
        TextureSemantic semantic = TextureSemantic::Color;
        bool srgb = false;
        std::uint64_t policy = 0; // TextureCompressionPolicy
        rhi::Format format = rhi::Format::Undefined;
        content::ContentDigest entry{}; // CompressedTextureStore key
    };

    // What the index knows of one version of a source image file.
    struct TextureSourceRecord
    {
        std::uint32_t width = 0, height = 0;
        int channels = 0;
        TextureMeans means;
        std::vector<TextureSourceSampling> samplings;

        [[nodiscard]] const TextureSourceSampling *Find(TextureSemantic semantic, bool srgb, std::uint64_t policy) const;
    };

    // Records live as small files beside a compressed store (a missing, stale or damaged one is a miss). A writable
    // index knows files by path, size and modification time; a read-only layer (an exported game's, inside its
    // content pack) by path relative to its content root and size. Thread-safe.
    class TextureSourceIndex
    {
    public:
        struct Layer
        {
            std::filesystem::path directory;   // where its records are
            std::filesystem::path contentRoot; // what its file paths are relative to
        };

        // `directory` is written (empty: never). Read-only layers are searched first, through the content layer.
        explicit TextureSourceIndex(std::filesystem::path directory, std::vector<Layer> readOnlyLayers = {});

        // Where the records of a compressed store's sources live: beside its entries.
        [[nodiscard]] static std::filesystem::path DirectoryBeside(const std::filesystem::path &storeDirectory);

        // The index shared by everything using these settings (records under the cache directory, the cooked roots'
        // as read-only layers); null when compression is disabled.
        [[nodiscard]] static std::shared_ptr<TextureSourceIndex> ForSettings(const TextureCompressionSettings &settings);

        // The record of `file` at `version` (what a texture's content is, Texture::GetFileVersion); nullopt when unknown.
        [[nodiscard]] std::optional<TextureSourceRecord> Find(const std::filesystem::path &file, const TextureFileVersion &version) const;
        // Adds (or replaces) a sampling of `file` at `version` in the writable index.
        bool Record(const std::filesystem::path &file, const TextureFileVersion &version, std::uint32_t width,
                    std::uint32_t height, int channels, const TextureMeans &means, const TextureSourceSampling &sampling) const;
        // The same into a read-only layer's directory (a cook writing an exported game's index).
        static bool RecordInLayer(const Layer &layer, const std::filesystem::path &file, std::uint32_t width,
                                  std::uint32_t height, int channels, const TextureMeans &means,
                                  const TextureSourceSampling &sampling);

        // The version of `file` now, through the content layer; nullopt when it cannot be read.
        [[nodiscard]] static std::optional<TextureFileVersion> CurrentVersion(const std::filesystem::path &file);

    private:
        std::filesystem::path m_directory;
        std::vector<Layer> m_layers;
        // Records read or written this session, by record key (misses too), and one writer at a time.
        mutable std::mutex m_mutex;
        mutable std::unordered_map<std::string, std::optional<TextureSourceRecord>> m_known;
    };
}
