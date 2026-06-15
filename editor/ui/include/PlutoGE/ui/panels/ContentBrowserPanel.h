#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <array>

namespace PlutoGE::ui
{
    inline constexpr const char *kContentBrowserAssetDragDropPayload = "PLUTOGE_CONTENT_BROWSER_ASSET";

    class ContentBrowserPanel : public Panel
    {
    public:
        ContentBrowserPanel(const PanelConfig &config) : Panel(config) {}
        ~ContentBrowserPanel() override = default;

        void Render() override;

    private:
        std::array<char, 160> m_filterBuffer{};
        std::array<char, 96> m_newMaterialNameBuffer{};
        int m_selectedAssetIndex = -1;
    };
}
