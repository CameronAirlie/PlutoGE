#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <array>

namespace PlutoGE::ui
{
    class ConsolePanel : public Panel
    {
    public:
        ConsolePanel(const PanelConfig &config) : Panel(config) {}
        ~ConsolePanel() override = default;

        void Render() override;

    private:
        std::array<char, 128> m_filterBuffer{};
        bool m_showInfo = true;
        bool m_showWarnings = true;
        bool m_showErrors = true;
        bool m_autoScroll = true;
    };
}
