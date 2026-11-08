#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    // Asset kinds the editor reloads in place when their files change outside the editor (an image editor saving a
    // texture, a material edited in a text editor, a sound re-exported). Models have their own import watch.
    enum class HotReloadKind
    {
        Texture,
        Material,
        Audio,
    };

    [[nodiscard]] std::optional<HotReloadKind> ClassifyHotReloadPath(const std::filesystem::path &path);

    // Polling file watch with a quiet period, for files under the asset directory. Capture runs on a worker;
    // Observe is cheap and IO-free.
    class AssetFileWatch
    {
    public:
        using Clock = std::chrono::steady_clock;
        using Stamp = std::pair<std::filesystem::file_time_type, std::uintmax_t>;
        using Snapshot = std::map<std::string, Stamp>;

        // Files under `assetRoot` that ClassifyHotReloadPath accepts; infrastructure folders are skipped.
        [[nodiscard]] static Snapshot Capture(const std::filesystem::path &assetRoot);

        // The first snapshot is the baseline. Changed or added files are returned once they have stayed the same
        // for the quiet period (so a file is not reloaded while it is still being written); deletions are ignored.
        [[nodiscard]] std::vector<std::string> Observe(Snapshot snapshot, Clock::time_point now);
        void Reset() { *this = {}; }

    private:
        Snapshot m_committed;
        Snapshot m_candidate;
        Clock::time_point m_candidateSince{};
        bool m_primed = false;
    };
}
