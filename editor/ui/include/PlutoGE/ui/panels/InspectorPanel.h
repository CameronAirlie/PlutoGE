#pragma once

#include "PlutoGE/ui/panels/Panel.h"

namespace PlutoGE::scene
{
    class CameraComponent;
    struct Property;
}

namespace PlutoGE::ui
{
    class InspectorPanel : public Panel
    {
    public:
        InspectorPanel(const PanelConfig &config) : Panel(config) {}
        ~InspectorPanel() override = default;

        void Initialize() override;
        void Render() override;
        void Shutdown() override;

    private:
        bool RenderPropertyEditor(scene::Property &property) const;
        void RenderCameraPostProcessEditor(scene::CameraComponent &cameraComponent) const;
    };
}