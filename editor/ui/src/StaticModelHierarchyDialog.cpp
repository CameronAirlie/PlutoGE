#include "PlutoGE/ui/StaticModelHierarchyDialog.h"
#include "PlutoGE/ui/StaticModelHierarchyPlacement.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/SceneFormat.h"
#include "PlutoGE/core/Engine.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>

namespace PlutoGE::ui
{
    struct StaticModelHierarchyDialog::State
    {
        std::array<char, 512> destination{};
        std::string error;
        std::optional<StaticModelHierarchySnapshot> snapshot;
        bool allowVersion4 = false;
    };
    StaticModelHierarchyDialog::StaticModelHierarchyDialog() : m_state(std::make_unique<State>()) {}
    StaticModelHierarchyDialog::~StaticModelHierarchyDialog() = default;

    void StaticModelHierarchyDialog::Render(const std::string &sourceReference)
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        auto *scene = shell.GetScene();
        if (!project) return;
        auto &state = *m_state;
        const bool canPlace = scene && !shell.GetEngine().IsRuntimeRunning() && !shell.IsModelImportRunning() &&
            project->GetManifest().assetPipelineVersion >= 3;
        ImGui::BeginDisabled(!canPlace);
        if (ImGui::Button("Add Static Hierarchy Snapshot..."))
        {
            state = {};
            StaticModelHierarchySnapshot snapshot;
            if (PrepareStaticModelHierarchySnapshot(*project, sourceReference, snapshot, state.error)) state.snapshot = std::move(snapshot);
            const auto stem = std::filesystem::path(sourceReference).stem().string();
            std::string destination;
            for (unsigned suffix = 0; ; ++suffix)
            {
                destination = "project://Snapshots/" + stem + "_Hierarchy" + (suffix ? "_" + std::to_string(suffix) : "") + ".plutomesh";
                std::error_code ec;
                if (!std::filesystem::exists(project->ResolveAssetReference(destination), ec) && !ec) break;
                if (ec || suffix >= 10000) { state.error = "Cannot choose an available snapshot destination."; break; }
            }
            std::copy_n(destination.data(), std::min(destination.size(), state.destination.size() - 1), state.destination.data());
            ImGui::OpenPopup("Static Hierarchy Snapshot");
        }
        ImGui::EndDisabled();
        if (!canPlace && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Open an editable scene in a version 3/4 project and wait for any active import to finish.");
        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Static Hierarchy Snapshot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped("Create editable scene nodes from the selected source scene. All geometry bindings share one independent authored mesh asset.");
        ImGui::TextWrapped("Source reimports will leave snapshot geometry unchanged. Material references stay shared. Scene undo removes the hierarchy; the extracted mesh stays in Assets.");
        if (state.snapshot)
            ImGui::Text("Source nodes: %zu | Geometry bindings: %zu", state.snapshot->layout.nodes.size(), state.snapshot->layout.bindings.size());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##SnapshotMesh", state.destination.data(), state.destination.size());
        ImGui::TextDisabled("Destination must be a new project:// mesh asset (.plutomesh).");
        const bool needsConversion = project->GetManifest().assetPipelineVersion < assets::kAffineSceneProjectVersion;
        if (needsConversion)
        {
            ImGui::Separator();
            ImGui::TextWrapped("Exact hierarchy transforms require project version 4. Enabling it saves the project manifest; older editor/runtime builds cannot open or export this project. Existing asset files are preserved.");
            ImGui::Checkbox("Enable project version 4", &state.allowVersion4);
        }
        if (!state.error.empty()) ImGui::TextWrapped("%s", state.error.c_str());
        ImGui::BeginDisabled(!canPlace || !state.snapshot || (needsConversion && !state.allowVersion4));
        if (ImGui::Button("Create Snapshot and Add"))
        {
            bool ready = true;
            if (needsConversion)
            {
                const auto previousVersion = project->GetManifest().assetPipelineVersion;
                project->GetManifest().assetPipelineVersion = assets::kAffineSceneProjectVersion;
                if (!project->Save(&state.error))
                { project->GetManifest().assetPipelineVersion = previousVersion; ready = false; }
                else shell.GetEngine().GetAssetManager().SetProjectAssetPipelineVersion(assets::kAffineSceneProjectVersion);
            }
            if (ready)
            {
                const auto &camera = shell.GetEditorCamera();
                auto rotation = glm::rotate(glm::mat4(1), glm::radians(camera.yawDegrees), glm::vec3(0, 1, 0));
                rotation = glm::rotate(rotation, glm::radians(camera.pitchDegrees), glm::vec3(1, 0, 0));
                scene::Entity *created = nullptr;
                shell.ExecuteSceneEdit("Add Static Hierarchy Snapshot", [&]
                {
                    created = PlaceStaticModelHierarchySnapshot(*project, *state.snapshot, state.destination.data(),
                        shell.GetEngine().GetAssetManager(), *scene, {camera.position, -glm::vec3(rotation[2])}, state.error);
                });
                // Also refresh after failure: a valid authored extraction can remain
                // if later resource loading or placement fails. Never hide that asset.
                shell.RefreshProjectAssets();
                if (created)
                {
                    shell.SetSelectedEntity(created);
                    shell.MarkProjectDirty();
                    shell.Log(EditorShell::ConsoleSeverity::Info, "Added independent static hierarchy snapshot: " + std::string(state.destination.data()));
                    ImGui::CloseCurrentPopup();
                }
                else shell.Log(EditorShell::ConsoleSeverity::Error, state.error);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
