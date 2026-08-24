#pragma once
#include "PlutoGE/ui/panels/Panel.h"
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    struct InputMappingBinding
    {
        int kind = 0, key = 0, button = 0, axis = 0, gamepad = 0, mouseButton = 0, mouseAxis = 0;
        float scale = 1.0f, deadZone = 0.15f;
    };
    struct InputMappingAction { std::string name; std::vector<InputMappingBinding> bindings; };

    class InputMappingEditorPanel : public Panel
    {
    public:
        InputMappingEditorPanel(const PanelConfig &config) : Panel(config) {}
        void Render() override;
    private:
        void Load();
        bool Save();
        std::string m_loadedReference;
        std::vector<InputMappingAction> m_actions;
        bool m_dirty = false;
    };
}
