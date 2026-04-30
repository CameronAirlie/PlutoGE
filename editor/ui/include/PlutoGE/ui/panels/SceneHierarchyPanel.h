#pragma once

#include "PlutoGE/ui/panels/Panel.h"

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::ui
{
    class PanelManager;
    class SceneHierarchyPanel : public Panel
    {
    public:
        SceneHierarchyPanel(const PanelConfig &config) : Panel(config) {}
        ~SceneHierarchyPanel() override = default;

        void Initialize() override;
        void Render() override;
        void Shutdown() override;

        void ContextMenu();
    };
}