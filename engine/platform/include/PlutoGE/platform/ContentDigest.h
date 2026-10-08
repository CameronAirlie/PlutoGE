#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace PlutoGE::content
{
    using ContentDigest = std::array<std::uint8_t, 32>;

    // Streaming SHA-256 for artifact addressing and integrity validation.
    // Finalize is non-destructive so the same prefix can be extended/reused.
    class ContentHasher
    {
    public:
        void Update(std::span<const std::byte> bytes);
        ContentDigest Finalize() const;
    private:
        void Compress(const std::byte *block);
        std::array<std::uint32_t, 8> m_state{
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::array<std::byte, 64> m_buffer{};
        std::size_t m_buffered = 0;
        std::uint64_t m_size = 0;
    };

    ContentDigest HashContent(std::span<const std::byte> bytes);
    bool HashFileContent(const std::filesystem::path &path, ContentDigest &digest, std::string *errorMessage = nullptr);
    std::string DigestToHex(const ContentDigest &digest);
    bool ParseContentDigest(std::string_view hex, ContentDigest &digest);
}
