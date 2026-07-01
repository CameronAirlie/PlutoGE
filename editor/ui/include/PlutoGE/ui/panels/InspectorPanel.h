#pragma once

#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class Mesh;
}

namespace PlutoGE::scene
{
    class CameraComponent;
    class ScriptComponent;
    class Entity;
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
        bool RenderScriptComponentEditor(scene::ScriptComponent &scriptComponent, scene::Entity &entity) const;
        void RefreshFilteredSubmeshIndices(const render::Mesh &mesh);
        void RefreshMaterialSlotUsageSummaries(const scene::MeshComponent &meshComponent);

        const render::Mesh *m_inspectedSubmeshList = nullptr;
        int m_selectedSubmeshIndex = -1;
        std::array<char, 128> m_submeshFilter{};
        std::vector<std::size_t> m_filteredSubmeshIndices;
        std::string m_appliedSubmeshFilter;

        const render::Mesh *m_materialUsageMesh = nullptr;
        std::size_t m_materialUsageMaterialCount = 0;
        std::vector<std::string> m_materialSlotUsageSummaries;
        int m_selectedMaterialSlotIndex = 0;
    };
}
