#pragma once
#include "PlutoGE/ui/InputMappingDocument.h"
#include "PlutoGE/ui/panels/Panel.h"
#include <string>

namespace PlutoGE::ui
{
    class InputMappingEditorPanel : public Panel
    {
    public:
        InputMappingEditorPanel(const PanelConfig &config) : Panel(config) {}
        void Render() override;
    private:
        void Load();
        bool Save();
        void RenderReport();
        // Press-to-bind: the next key, mouse button or gamepad button pressed is assigned.
        void UpdateCapture();

        std::string m_loadedReference;
        InputMappingDocument m_document;
        std::string m_loadError;
        std::string m_saveError;
        char m_filter[64]{};
        int m_captureAction = -1;
        int m_captureBinding = -1;
        bool m_dirty = false;
    };
}
