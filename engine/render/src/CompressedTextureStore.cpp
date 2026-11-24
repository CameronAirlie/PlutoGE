#include "CompressedTextureStore.h"

#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/rhi/FormatInfo.h"

#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
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

    CompressedTextureStore::CompressedTextureStore(std::filesystem::path directory, std::vector<std::filesystem::path> readOnlyLayers)
        : m_directory(std::move(directory)), m_readOnlyLayers(std::move(readOnlyLayers))
    {
    }

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

    std::filesystem::path CompressedTextureStore::PathFor(const std::filesystem::path &directory, const content::ContentDigest &key)
    {
        const auto hex = content::DigestToHex(key);
        return directory / hex.substr(0, 2) / (hex + ".ptxc");
    }

    std::optional<EncodedTexture> CompressedTextureStore::Parse(std::span<const std::byte> bytes)
    {
        Header header;
        if (bytes.size() < sizeof(header))
            return std::nullopt;
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.magic != kMagic || header.version != kVersion)
            return std::nullopt;
        EncodedTexture texture;
        texture.format = static_cast<rhi::Format>(header.format);
        texture.width = header.width;
        texture.height = header.height;
        texture.mipLevels = header.mipLevels;
        if (!rhi::IsBlockCompressed(texture.format) || !texture.width || !texture.height || !texture.mipLevels ||
            header.byteCount != rhi::TextureChainByteSize(texture.format, texture.width, texture.height, texture.mipLevels) ||
            bytes.size() - sizeof(header) != header.byteCount)
            return std::nullopt;
        texture.data.assign(bytes.begin() + sizeof(header), bytes.end());
        return texture;
    }

    std::optional<EncodedTexture> CompressedTextureStore::Load(const content::ContentDigest &key) const
    {
        std::string bytes;
        for (const auto &layer : m_readOnlyLayers)
            if (!layer.empty() && content::Exists(PathFor(layer, key)) && content::ReadFile(PathFor(layer, key), bytes))
                if (auto texture = Parse(std::as_bytes(std::span(bytes.data(), bytes.size()))))
                    return texture;
        if (m_directory.empty())
            return std::nullopt;
        std::ifstream input(PathFor(m_directory, key), std::ios::binary);
        if (!input)
            return std::nullopt;
        bytes.assign(std::istreambuf_iterator<char>(input), {});
        return Parse(std::as_bytes(std::span(bytes.data(), bytes.size())));
    }

    bool CompressedTextureStore::Save(const content::ContentDigest &key, const EncodedTexture &texture) const
    {
        if (m_directory.empty())
            return false;
        const auto path = PathFor(m_directory, key);
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
