#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene::volume
{
    // Several volumes' edits in one blob, each under a name: a terrain world's tiles ("x,z"), or a scene's worlds and
    // lone volumes for a save game. The entries are opaque bytes (usually VolumeFile contents, or nested archives).
    //
    // Version 1, little endian: "PLVA", version (u32), entry count (u32), then per entry in name order: name length
    // (u32), name (UTF-8), size (u32), bytes.
    namespace VolumeEditArchive
    {
        inline constexpr std::uint32_t Version = 1;
        using Entries = std::map<std::string, std::vector<std::uint8_t>, std::less<>>;

        [[nodiscard]] std::vector<std::uint8_t> Encode(const Entries &entries);
        // nullopt (with a reason) when the bytes are not an archive, are damaged, or come from a newer version.
        [[nodiscard]] std::optional<Entries> Decode(std::span<const std::uint8_t> bytes, std::string *error = nullptr);
    }
}
