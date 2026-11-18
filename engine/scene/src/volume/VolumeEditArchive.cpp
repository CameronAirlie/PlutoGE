#include "PlutoGE/scene/volume/VolumeEditArchive.h"

#include <array>
#include <cstring>

namespace PlutoGE::scene::volume::VolumeEditArchive
{
    namespace
    {
        constexpr std::array<std::uint8_t, 4> Magic{'P', 'L', 'V', 'A'};

        void WriteU32(std::vector<std::uint8_t> &out, std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
                out.push_back(static_cast<std::uint8_t>(value >> shift));
        }

        class Reader
        {
        public:
            explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}
            bool U32(std::uint32_t &value)
            {
                if (Remaining() < 4)
                    return false;
                value = 0;
                for (int index = 0; index < 4; ++index)
                    value |= static_cast<std::uint32_t>(m_bytes[m_offset + index]) << (8 * index);
                m_offset += 4;
                return true;
            }
            bool Bytes(std::size_t count, std::span<const std::uint8_t> &out)
            {
                if (Remaining() < count)
                    return false;
                out = m_bytes.subspan(m_offset, count);
                m_offset += count;
                return true;
            }
            [[nodiscard]] std::size_t Remaining() const { return m_bytes.size() - m_offset; }

        private:
            std::span<const std::uint8_t> m_bytes;
            std::size_t m_offset = 0;
        };
    }

    std::vector<std::uint8_t> Encode(const Entries &entries)
    {
        std::vector<std::uint8_t> out(Magic.begin(), Magic.end());
        WriteU32(out, Version);
        WriteU32(out, static_cast<std::uint32_t>(entries.size()));
        for (const auto &[name, bytes] : entries)
        {
            WriteU32(out, static_cast<std::uint32_t>(name.size()));
            out.insert(out.end(), name.begin(), name.end());
            WriteU32(out, static_cast<std::uint32_t>(bytes.size()));
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
        return out;
    }

    std::optional<Entries> Decode(std::span<const std::uint8_t> bytes, std::string *error)
    {
        const auto fail = [&](const char *reason) -> std::optional<Entries> {
            if (error)
                *error = reason;
            return std::nullopt;
        };
        if (bytes.size() < Magic.size() || std::memcmp(bytes.data(), Magic.data(), Magic.size()) != 0)
            return fail("Not a terrain volume edit archive.");
        Reader reader(bytes.subspan(Magic.size()));
        std::uint32_t version = 0, count = 0;
        if (!reader.U32(version) || !reader.U32(count))
            return fail("The edit archive is cut short.");
        if (version == 0 || version > Version)
            return fail("The edit archive comes from a newer version.");
        // Every entry takes at least its two lengths: a count larger than that is damage, not a reason to allocate.
        if (count > reader.Remaining() / 8)
            return fail("The edit archive's entry count is damaged.");
        Entries entries;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::uint32_t nameSize = 0, size = 0;
            std::span<const std::uint8_t> name, data;
            if (!reader.U32(nameSize) || !reader.Bytes(nameSize, name) || !reader.U32(size) || !reader.Bytes(size, data))
                return fail("The edit archive is cut short.");
            entries.insert_or_assign(std::string(name.begin(), name.end()), std::vector<std::uint8_t>(data.begin(), data.end()));
        }
        if (reader.Remaining() != 0)
            return fail("The edit archive has trailing data.");
        return entries;
    }
}
