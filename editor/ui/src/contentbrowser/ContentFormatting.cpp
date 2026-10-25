#include "PlutoGE/ui/contentbrowser/ContentFormatting.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace PlutoGE::ui
{
    std::string FormatContentSize(std::uint64_t bytes)
    {
        constexpr std::array kUnits{"B", "KB", "MB", "GB", "TB"};
        if (bytes < 1024)
        {
            return std::to_string(bytes) + " B";
        }
        double value = static_cast<double>(bytes);
        std::size_t unit = 0;
        while (value >= 1024.0 && unit + 1 < kUnits.size())
        {
            value /= 1024.0;
            ++unit;
        }
        char text[32];
        std::snprintf(text, sizeof(text), value < 10.0 ? "%.1f %s" : "%.0f %s", value, kUnits[unit]);
        return text;
    }

    std::int64_t ToUnixSeconds(std::filesystem::file_time_type time)
    {
        const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
        return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
    }

    std::string FormatContentTimestamp(std::int64_t unixSeconds)
    {
        if (unixSeconds == 0)
        {
            return {};
        }
        const std::time_t time = static_cast<std::time_t>(unixSeconds);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
        return text;
    }
}
