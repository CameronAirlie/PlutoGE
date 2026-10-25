#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace PlutoGE::ui
{
    // "0 B", "512 B", "1.5 KB", "12 MB" ... (binary units, one decimal under 10).
    [[nodiscard]] std::string FormatContentSize(std::uint64_t bytes);

    // Seconds since the Unix epoch for a file time, or 0 when unavailable.
    [[nodiscard]] std::int64_t ToUnixSeconds(std::filesystem::file_time_type time);

    // Local "YYYY-MM-DD HH:MM", or an empty string for 0.
    [[nodiscard]] std::string FormatContentTimestamp(std::int64_t unixSeconds);
}
