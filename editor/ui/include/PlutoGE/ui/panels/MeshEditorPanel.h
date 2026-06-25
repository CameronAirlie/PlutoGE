#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <string>
#include <vector>

namespace PlutoGE::ui
{
    class MeshEditorPanel : public Panel
    {
    public:
        MeshEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~MeshEditorPanel() override = default;

        void Render() override;

    private:
        void LoadActiveMesh();

        std::string m_loadedReference;
        render::MeshConfig m_config;
        std::vector<std::string> m_materialReferences;
        bool m_dirty = false;
    };
}
