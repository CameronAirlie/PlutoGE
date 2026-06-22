#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <string>

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    inline constexpr const char *kContentBrowserAssetDragDropPayload = "PLUTOGE_CONTENT_BROWSER_ASSET";
    inline constexpr const char *kContentBrowserMeshSubassetDragDropPayload = "PLUTOGE_MESH_SUBASSET";

    struct ContentBrowserMeshSubassetPayload
    {
        char sourceReference[512]{};
        int submeshIndex = -1;
        int submeshCount = 1;
        int materialSlot = -1;
    };

    bool InstantiateMeshAssetIntoScene(std::string reference, scene::Entity *parent = nullptr, int submeshIndex = -1, int submeshCount = 1, int materialSlot = -1);

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
