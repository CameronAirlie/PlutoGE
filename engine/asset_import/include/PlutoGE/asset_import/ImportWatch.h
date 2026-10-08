#pragma once
#include "PlutoGE/assets/Project.h"
#include <chrono>
#include <map>
#include <stop_token>
#include <vector>

namespace PlutoGE::assetimport
{
    struct ImportWatchStamp
    {
        std::filesystem::file_type type = std::filesystem::file_type::not_found;
        std::filesystem::file_time_type modified{};
        std::uintmax_t size = 0;
        bool operator==(const ImportWatchStamp &) const = default;
    };
    using ImportWatchSnapshot = std::map<std::filesystem::path, ImportWatchStamp>;
    // Cheap read-only event hints. Content hashes and provenance remain the
    // reconciliation authority. Call under the project lock on a worker.
    bool CaptureImportWatchSnapshot(const assets::Project &project, ImportWatchSnapshot &snapshot,
                                     std::string *errorMessage = nullptr, std::stop_token stop = {});

    // IO-free quiet-period coalescing, independent of a native watcher backend.
    // First observation establishes a baseline. Changes are emitted only after
    // two matching observations separated by the quiet period; reversions vanish.
    class ImportWatchDebouncer
    {
    public:
        using Clock = std::chrono::steady_clock;
        explicit ImportWatchDebouncer(Clock::duration quietPeriod = std::chrono::milliseconds(500)) : m_quietPeriod(quietPeriod) {}
        std::vector<std::filesystem::path> Observe(ImportWatchSnapshot snapshot, Clock::time_point now);
        void Reset();
    private:
        Clock::duration m_quietPeriod;
        Clock::time_point m_lastChange{};
        ImportWatchSnapshot m_committed, m_candidate;
        bool m_initialized = false;
    };
}
