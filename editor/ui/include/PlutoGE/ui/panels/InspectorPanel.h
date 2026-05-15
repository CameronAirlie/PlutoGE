#pragma once

#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/Panel.h"

namespace PlutoGE::scene
{
    class CameraComponent;
    struct Property;
    class Scene;
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
        void RenderEditorCameraInspector(EditorShell::EditorViewportCamera &camera) const;
        void RenderEditorCameraPostProcessEditor(EditorShell::EditorViewportCamera &camera) const;
        void RenderSceneEnvironmentInspector(scene::Scene &scene) const;
    };
}