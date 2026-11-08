#pragma once

#include "PlutoGE/ui/ConsoleLog.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace PlutoGE::ui
{
    // Lists ConsoleLog entries: severity and source filters, text search, collapse of repeated messages, a detail
    // pane for the selected entry, and navigation to the entity, asset or source line an entry refers to.
    class ConsolePanel : public Panel
    {
    public:
        ConsolePanel(const PanelConfig &config);
        ~ConsolePanel() override = default;

        void Render() override;

    private:
        void RenderToolbar(const std::array<uint64_t, 3> &counts);
        void RenderEntries(float height);
        void RenderDetails();
        void RebuildVisibleEntries();
        [[nodiscard]] bool PassesFilter(const ConsoleEntry &entry) const;
        [[nodiscard]] const ConsoleEntry *FindSelected() const;
        void SaveSettings() const;

        // Snapshot of the log, refreshed only when its revision changes.
        std::vector<ConsoleEntry> m_entries;
        uint64_t m_revision = ~uint64_t{0};
        // Indices into m_entries, or into m_collapsed when collapsing.
        std::vector<ConsoleEntry> m_collapsed;
        std::vector<std::size_t> m_visible;
        bool m_visibleDirty = true;

        std::array<char, 128> m_filterBuffer{};
        std::array<bool, 3> m_showSeverity{true, true, true};
        std::array<bool, kConsoleSourceCount> m_showSource{true, true, true, true, true};
        bool m_collapse = false;
        bool m_autoScroll = true;
        bool m_scrollToBottom = false;
        uint64_t m_selectedSequence = 0;
        float m_detailsHeight = 120.0f;
    };
}
