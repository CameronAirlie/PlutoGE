#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    enum class ConsoleSeverity
    {
        Info,
        Warning,
        Error,
    };

    // Where a message came from; the Console filters and labels by it.
    enum class ConsoleSource
    {
        Editor,
        Script,      // Debug.Log and script exceptions
        ScriptBuild, // C# compiler output
        Import,
        Build,       // Game export
    };

    inline constexpr std::size_t kConsoleSourceCount = 5;

    // EditorSettings key: pause play mode when a script callback throws.
    inline constexpr std::string_view kPauseOnScriptErrorKey = "console.pauseOnError";

    [[nodiscard]] const char *ConsoleSeverityLabel(ConsoleSeverity severity);
    [[nodiscard]] const char *ConsoleSourceLabel(ConsoleSource source);

    // A source-code position the Console can open in the configured script editor.
    struct ConsoleFileLocation
    {
        std::filesystem::path file;
        int line = 0;
        int column = 0;

        [[nodiscard]] bool IsSet() const { return !file.empty(); }
        bool operator==(const ConsoleFileLocation &) const = default;
    };

    struct ConsoleEntry
    {
        ConsoleSeverity severity = ConsoleSeverity::Info;
        ConsoleSource source = ConsoleSource::Editor;
        std::string text;          // One-line summary shown in the list.
        std::string details;       // Optional multi-line body (stack trace, full compiler line).
        uint32_t entityId = 0;     // Optional entity to select.
        std::string asset;         // Optional project:// reference to reveal.
        ConsoleFileLocation location;
        std::chrono::system_clock::time_point time{};
        uint64_t repeatCount = 1;  // Consecutive identical messages merged into this entry.
        uint64_t sequence = 0;     // Monotonic id, stable while the entry is in the log.

        // Same message from the same place: such entries merge instead of filling the log.
        [[nodiscard]] bool SameMessage(const ConsoleEntry &other) const;
    };

    // Thread-safe, bounded store of Console entries. Scripts, builds and imports log from worker threads; the
    // Console panel reads a snapshot when the revision changes.
    class ConsoleLog
    {
    public:
        explicit ConsoleLog(std::size_t capacity = 2000);

        void Append(ConsoleEntry entry);
        void Clear();
        // Drops every entry from `source`, e.g. the previous build's compiler messages when a new build reports.
        void RemoveSource(ConsoleSource source);

        // Increments on every change, so readers copy only when something happened.
        [[nodiscard]] uint64_t GetRevision() const;
        // Copies the entries into `out` when the log changed since `revision`; returns whether it copied.
        bool CopyIfChanged(uint64_t &revision, std::vector<ConsoleEntry> &out) const;
        [[nodiscard]] std::vector<ConsoleEntry> Snapshot() const;
        // Message counts per severity, including merged repeats.
        [[nodiscard]] std::array<uint64_t, 3> CountBySeverity() const;

    private:
        mutable std::mutex m_mutex;
        std::vector<ConsoleEntry> m_entries;
        std::size_t m_capacity;
        uint64_t m_revision = 0;
        uint64_t m_nextSequence = 1;
    };

    // Merges entries with the same message regardless of position (the Console's "Collapse" view). The first
    // occurrence keeps its place; repeat counts add up and the time becomes the latest occurrence.
    [[nodiscard]] std::vector<ConsoleEntry> CollapseConsoleEntries(const std::vector<ConsoleEntry> &entries);
}
