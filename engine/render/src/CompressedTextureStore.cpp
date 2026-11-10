#include "CompressedTextureStore.h"

#include "PlutoGE/render/rhi/FormatInfo.h"

#include <atomic>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

namespace PlutoGE::render
{
    namespace
    {
        constexpr std::uint32_t kMagic = 0x43585450; // "PTXC"
        // Bump when the file layout or BuildTextureMipChain's output changes.
        constexpr std::uint32_t kVersion = 1;

        struct Header
        {
            std::uint32_t magic = kMagic;
            std::uint32_t version = kVersion;
            std::uint32_t format = 0;
            std::uint32_t width = 0, height = 0, mipLevels = 0;
            std::uint64_t byteCount = 0;
        };

        template <class T>
        void HashValue(content::ContentHasher &hasher, const T &value)
        {
            hasher.Update(std::as_bytes(std::span(&value, 1)));
        }
    }

    CompressedTextureStore::CompressedTextureStore(std::filesystem::path directory) : m_directory(std::move(directory)) {}

    content::ContentDigest CompressedTextureStore::Key(std::span<const std::byte> rgba8, std::uint32_t width,
                                                       std::uint32_t height, TextureSemantic semantic, rhi::Format format)
    {
        content::ContentHasher hasher;
        HashValue(hasher, kVersion);
        HashValue(hasher, width);
        HashValue(hasher, height);
        HashValue(hasher, semantic);
        HashValue(hasher, format);
        if (const auto encoder = FindTextureBlockEncoder(format))
        {
            const auto identity = encoder->Identity();
            hasher.Update(std::as_bytes(std::span(identity.data(), identity.size())));
        }
        hasher.Update(rgba8);
        return hasher.Finalize();
    }

    std::filesystem::path CompressedTextureStore::PathFor(const content::ContentDigest &key) const
    {
        const auto hex = content::DigestToHex(key);
        return m_directory / hex.substr(0, 2) / (hex + ".ptxc");
    }

    std::optional<EncodedTexture> CompressedTextureStore::Load(const content::ContentDigest &key) const
    {
        if (m_directory.empty())
            return std::nullopt;
        std::ifstream input(PathFor(key), std::ios::binary);
        if (!input)
            return std::nullopt;
        Header header;
        if (!input.read(reinterpret_cast<char *>(&header), sizeof(header)) || header.magic != kMagic ||
            header.version != kVersion)
            return std::nullopt;
        EncodedTexture texture;
        texture.format = static_cast<rhi::Format>(header.format);
        texture.width = header.width;
        texture.height = header.height;
        texture.mipLevels = header.mipLevels;
        if (!rhi::IsBlockCompressed(texture.format) || !texture.width || !texture.height || !texture.mipLevels ||
            header.byteCount != rhi::TextureChainByteSize(texture.format, texture.width, texture.height, texture.mipLevels))
            return std::nullopt;
        texture.data.resize(static_cast<std::size_t>(header.byteCount));
        if (!input.read(reinterpret_cast<char *>(texture.data.data()), static_cast<std::streamsize>(texture.data.size())))
            return std::nullopt;
        return texture;
    }

    bool CompressedTextureStore::Save(const content::ContentDigest &key, const EncodedTexture &texture) const
    {
        if (m_directory.empty())
            return false;
        const auto path = PathFor(key);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;
        static std::atomic<std::uint64_t> sequence{0};
        auto temporary = path;
        temporary += ".tmp" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "-" +
                     std::to_string(sequence.fetch_add(1));
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            Header header;
            header.format = static_cast<std::uint32_t>(texture.format);
            header.width = texture.width;
            header.height = texture.height;
            header.mipLevels = texture.mipLevels;
            header.byteCount = texture.data.size();
            output.write(reinterpret_cast<const char *>(&header), sizeof(header));
            output.write(reinterpret_cast<const char *>(texture.data.data()), static_cast<std::streamsize>(texture.data.size()));
            if (!output)
            {
                output.close();
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        // Another thread or process may have written the same entry meanwhile; either copy is correct.
        std::filesystem::rename(temporary, path, error);
        if (error)
            std::filesystem::remove(temporary, error);
        return true;
    }
}
