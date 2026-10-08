#include "PlutoGE/platform/ContentDigest.h"

#include <algorithm>
#include <bit>
#include <fstream>

namespace PlutoGE::content
{
    namespace
    {
        constexpr std::array<std::uint32_t, 64> RoundConstants{
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        int HexValue(char value)
        {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        }
    }

    void ContentHasher::Compress(const std::byte *block)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index)
            for (std::size_t byte = 0; byte < 4; ++byte)
                words[index] = (words[index] << 8) | std::to_integer<std::uint32_t>(block[index * 4 + byte]);
        for (std::size_t index = 16; index < 64; ++index)
        {
            const auto a = words[index - 15];
            const auto b = words[index - 2];
            const auto small0 = std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3);
            const auto small1 = std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10);
            words[index] = words[index - 16] + small0 + words[index - 7] + small1;
        }
        auto [a,b,c,d,e,f,g,h] = m_state;
        for (std::size_t index = 0; index < 64; ++index)
        {
            const auto big1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choose = (e & f) ^ (~e & g);
            const auto first = h + big1 + choose + RoundConstants[index] + words[index];
            const auto big0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto second = big0 + majority;
            h = g; g = f; f = e; e = d + first;
            d = c; c = b; b = a; a = first + second;
        }
        m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
        m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
    }

    void ContentHasher::Update(std::span<const std::byte> bytes)
    {
        m_size += static_cast<std::uint64_t>(bytes.size());
        while (!bytes.empty())
        {
            if (m_buffered == 0 && bytes.size() >= m_buffer.size())
            {
                Compress(bytes.data());
                bytes = bytes.subspan(m_buffer.size());
                continue;
            }
            const auto count = std::min(bytes.size(), m_buffer.size() - m_buffered);
            std::copy_n(bytes.data(), count, m_buffer.data() + m_buffered);
            m_buffered += count;
            bytes = bytes.subspan(count);
            if (m_buffered == m_buffer.size())
            {
                Compress(m_buffer.data());
                m_buffered = 0;
            }
        }
    }

    ContentDigest ContentHasher::Finalize() const
    {
        auto finalized = *this;
        const auto bitSize = m_size * 8;
        std::array<std::byte, 64> padding{};
        padding[0] = std::byte{0x80};
        const auto paddingSize = m_buffered < 56 ? 56 - m_buffered : 120 - m_buffered;
        finalized.Update(std::span(padding).first(paddingSize));
        std::array<std::byte, 8> length{};
        for (std::size_t index = 0; index < 8; ++index)
            length[7 - index] = static_cast<std::byte>((bitSize >> (index * 8)) & 255);
        finalized.Update(length);
        ContentDigest digest;
        for (std::size_t index = 0; index < digest.size(); ++index)
            digest[index] = static_cast<std::uint8_t>(finalized.m_state[index / 4] >> ((3 - index % 4) * 8));
        return digest;
    }

    ContentDigest HashContent(std::span<const std::byte> bytes)
    {
        ContentHasher hasher;
        hasher.Update(bytes);
        return hasher.Finalize();
    }

    bool HashFileContent(const std::filesystem::path &path, ContentDigest &digest, std::string *errorMessage)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            if (errorMessage) *errorMessage = "Cannot open input for hashing: " + path.string();
            return false;
        }
        ContentHasher hasher;
        std::array<char, 64 * 1024> buffer;
        while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0)
            hasher.Update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(input.gcount()))));
        if (!input.eof())
        {
            if (errorMessage) *errorMessage = "Cannot finish hashing input: " + path.string();
            return false;
        }
        digest = hasher.Finalize();
        if (errorMessage) errorMessage->clear();
        return true;
    }

    std::string DigestToHex(const ContentDigest &digest)
    {
        constexpr char Hex[] = "0123456789abcdef";
        std::string text;
        text.reserve(64);
        for (const auto value : digest)
        {
            text += Hex[value >> 4];
            text += Hex[value & 15];
        }
        return text;
    }

    bool ParseContentDigest(std::string_view hex, ContentDigest &digest)
    {
        if (hex.size() != 64) return false;
        ContentDigest parsed;
        for (std::size_t index = 0; index < parsed.size(); ++index)
        {
            const int high = HexValue(hex[index * 2]);
            const int low = HexValue(hex[index * 2 + 1]);
            if (high < 0 || low < 0) return false;
            parsed[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        digest = parsed;
        return true;
    }
}
