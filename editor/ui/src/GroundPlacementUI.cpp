#include "PlutoGE/ui/EditorShell.h"
#include <imgui.h>

namespace PlutoGE::ui
{
    void EditorShell::RenderGroundPlacement()
    {
        if (!m_showGroundPlacement) return;
        ImGui::SetNextWindowSize(ImVec2(440, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Drop Selection onto Ground", &m_showGroundPlacement))
        {
            ImGui::End();
            return;
        }
        auto *entity = GetSelectedEntity();
        ImGui::TextWrapped("Selection: %s", entity ? entity->GetName().c_str() : "None");
        auto &options = m_groundPlacementOptions;
        ImGui::DragFloat("Maximum distance", &options.maxDistance, 1, 0.01f, 100000);
        ImGui::DragFloat("Surface offset", &options.surfaceOffset, 0.01f);
        ImGui::Checkbox("Align up axis to surface", &options.alignToNormal);
        ImGui::Checkbox("Place pivot on surface", &options.usePivot);
        ImGui::SliderFloat("Random yaw (+/- degrees)", &options.randomYawDegrees, 0, 180);
        ImGui::DragFloatRange2("Scale factor", &options.minScaleFactor, &options.maxScaleFactor, 0.01f, 0.01f, 100);
        ImGui::InputScalar("Random seed", ImGuiDataType_U32, &options.seed);
        ImGui::TextWrapped("Targets need colliders. Bounds placement uses static mesh bounds and primitive colliders, including children. "
                           "Scale factors multiply the current scale. Each successful drop advances the seed.");
        const bool unavailable = !entity || !m_scene || m_engine.IsRuntimeRunning() ||
                                 (m_activeBakeTask && m_activeBakeTask->IsRunning());
        ImGui::BeginDisabled(unavailable);
        if (ImGui::Button("Drop Selection"))
        {
            scene::Transform transform;
            if (GroundPlacement::Compute(*m_scene, *entity, options, transform, m_groundPlacementError))
            {
                ExecuteSceneEdit("Drop Selection onto Ground", [&]() {
                    if (entity->GetPosition() != transform.position)
                    {
                        entity->SetPosition(transform.position);
                        entity->AddPrefabOverride("Transform.Position");
                    }
                    if (entity->GetRotation() != transform.rotation)
                    {
                        entity->SetRotation(transform.rotation);
                        entity->AddPrefabOverride("Transform.Rotation");
                    }
                    if (entity->GetScale() != transform.scale)
                    {
                        entity->SetScale(transform.scale);
                        entity->AddPrefabOverride("Transform.Scale");
                    }
                });
                ++options.seed;
            }
        }
        ImGui::EndDisabled();
        if (!m_groundPlacementError.empty()) ImGui::TextWrapped("%s", m_groundPlacementError.c_str());
        ImGui::End();
    }
}
