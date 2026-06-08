#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <glm/glm.hpp>

#include <string>

namespace PlutoGE::ui
{
    class MaterialEditorPanel : public Panel
    {
    public:
        MaterialEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~MaterialEditorPanel() override = default;

        void Render() override;

    private:
        void LoadActiveMaterial();

        std::string m_loadedReference;
        glm::vec4 m_color{1.0f};
        float m_metallic = 0.0f;
        float m_roughness = 0.55f;
        bool m_flipNormalY = false;
        bool m_dirty = false;
    };
}
