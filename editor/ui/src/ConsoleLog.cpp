#include "PlutoGE/ui/ConsoleLog.h"

#include <algorithm>
#include <unordered_map>

namespace PlutoGE::ui
{
    const char *ConsoleSeverityLabel(ConsoleSeverity severity)
    {
        switch (severity)
        {
        case ConsoleSeverity::Warning:
            return "Warning";
        case ConsoleSeverity::Error:
            return "Error";
        case ConsoleSeverity::Info:
        default:
            return "Info";
        }
    }

    const char *ConsoleSourceLabel(ConsoleSource source)
    {
        switch (source)
        {
        case ConsoleSource::Script:
            return "Script";
        case ConsoleSource::ScriptBuild:
            return "Compiler";
        case ConsoleSource::Import:
            return "Import";
        case ConsoleSource::Build:
            return "Build";
        case ConsoleSource::Editor:
        default:
            return "Editor";
        }
    }

    bool ConsoleEntry::SameMessage(const ConsoleEntry &other) const
    {
        return severity == other.severity && source == other.source && entityId == other.entityId &&
               text == other.text && details == other.details && asset == other.asset && location == other.location;
    }

    ConsoleLog::ConsoleLog(std::size_t capacity) : m_capacity(std::max<std::size_t>(capacity, 1))
    {
    }

    void ConsoleLog::Append(ConsoleEntry entry)
    {
        if (entry.text.empty())
            return;
        if (entry.time == std::chrono::system_clock::time_point{})
            entry.time = std::chrono::system_clock::now();

        std::lock_guard lock(m_mutex);
        ++m_revision;
        if (!m_entries.empty() && m_entries.back().SameMessage(entry))
        {
            auto &last = m_entries.back();
            last.repeatCount += std::max<uint64_t>(entry.repeatCount, 1);
            last.time = entry.time;
            return;
        }

        entry.sequence = m_nextSequence++;
        entry.repeatCount = std::max<uint64_t>(entry.repeatCount, 1);
        m_entries.push_back(std::move(entry));
        if (m_entries.size() > m_capacity)
            m_entries.erase(m_entries.begin(), m_entries.begin() + static_cast<std::ptrdiff_t>(m_entries.size() - m_capacity));
    }

    void ConsoleLog::Clear()
    {
        std::lock_guard lock(m_mutex);
        m_entries.clear();
        ++m_revision;
    }

    void ConsoleLog::RemoveSource(ConsoleSource source)
    {
        std::lock_guard lock(m_mutex);
        const auto removed = std::erase_if(m_entries, [source](const ConsoleEntry &entry) { return entry.source == source; });
        if (removed > 0)
            ++m_revision;
    }

    uint64_t ConsoleLog::GetRevision() const
    {
        std::lock_guard lock(m_mutex);
        return m_revision;
    }

    bool ConsoleLog::CopyIfChanged(uint64_t &revision, std::vector<ConsoleEntry> &out) const
    {
        std::lock_guard lock(m_mutex);
        if (revision == m_revision)
            return false;
        out = m_entries;
        revision = m_revision;
        return true;
    }

    std::vector<ConsoleEntry> ConsoleLog::Snapshot() const
    {
        std::lock_guard lock(m_mutex);
        return m_entries;
    }

    std::array<uint64_t, 3> ConsoleLog::CountBySeverity() const
    {
        std::array<uint64_t, 3> counts{};
        std::lock_guard lock(m_mutex);
        for (const auto &entry : m_entries)
            counts[static_cast<std::size_t>(entry.severity)] += entry.repeatCount;
        return counts;
    }

    std::vector<ConsoleEntry> CollapseConsoleEntries(const std::vector<ConsoleEntry> &entries)
    {
        std::vector<ConsoleEntry> collapsed;
        collapsed.reserve(entries.size());
        // Bucket by text to keep the scan linear; SameMessage settles collisions within a bucket.
        std::unordered_map<std::string, std::vector<std::size_t>> byText;
        for (const auto &entry : entries)
        {
            auto &bucket = byText[entry.text];
            auto match = std::find_if(bucket.begin(), bucket.end(),
                                      [&](std::size_t index) { return collapsed[index].SameMessage(entry); });
            if (match != bucket.end())
            {
                auto &existing = collapsed[*match];
                existing.repeatCount += entry.repeatCount;
                existing.time = std::max(existing.time, entry.time);
                continue;
            }
            bucket.push_back(collapsed.size());
            collapsed.push_back(entry);
        }
        return collapsed;
    }
}
