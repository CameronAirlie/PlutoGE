#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

namespace PlutoGE::ui
{

    void RenderEntityNode(scene::Entity *entity)
    {
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (entity->GetChildren().empty())
        {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf;
        }

        if (EditorShell::GetInstance().GetSelectedEntity() == entity)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(entity->GetName().c_str()); // Ensure unique ID for ImGui tree node
        bool nodeOpen = ImGui::TreeNodeEx((void *)(intptr_t)entity->GetName().c_str(), nodeFlags, "%s", entity->GetName().c_str());

        // If clicked, set this entity as the selected entity in the editor shell
        if (ImGui::IsItemClicked() && ImGui::IsItemHovered())
        {
            EditorShell::GetInstance().SetSelectedEntity(entity);
        }

        // Context menu for right-clicking on an entity
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete Entity"))
            {
                auto scene = EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    scene->RemoveEntity(entity);
                    if (EditorShell::GetInstance().GetSelectedEntity() == entity)
                    {
                        EditorShell::GetInstance().SetSelectedEntity(nullptr); // Clear selection if the selected entity is deleted
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (nodeOpen)
        {
            for (auto child : entity->GetChildren())
            {
                RenderEntityNode(child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::Initialize()
    {
        // Initialization code for the scene hierarchy panel (e.g., load icons, set up data structures)
    }

    void SceneHierarchyPanel::Render()
    {
        const bool isEditorCameraSelected = EditorShell::GetInstance().IsEditorCameraSelected();
        if (ImGui::Selectable("Editor Camera", isEditorCameraSelected))
        {
            EditorShell::GetInstance().SelectEditorCamera();
        }

        ImGui::Separator();

        auto scene = ui::EditorShell::GetInstance().GetEngine().GetScene();
        if (!scene)
        {
            ImGui::Text("No scene loaded");
            return;
        }

        for (auto entity : scene->GetRootEntities())
        {
            RenderEntityNode(entity);
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered())
        {
            EditorShell::GetInstance().SetSelectedEntity(nullptr);
        }

        ContextMenu();
    }

    void SceneHierarchyPanel::Shutdown()
    {
        // Cleanup code for the scene hierarchy panel (e.g., release resources)
    }

    void SceneHierarchyPanel::ContextMenu()
    {
        if (ImGui::BeginPopupContextWindow("PanelContextMenu", ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                auto newEntity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "New Entity"});
                auto scene = ui::EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    scene->AddEntity(std::move(newEntity));
                }
            }
            if (ImGui::MenuItem("Create Cube"))
            {
                auto newEntity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Cube"});
                auto &engine = core::Engine::GetInstance();
                auto *scene = ui::EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    auto *entity = scene->AddEntity(std::move(newEntity));
                    auto *meshComponent = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                        .mesh = render::Mesh::Cube(),
                        .material = engine.GetAssetManager().CreateDefaultMaterial(),
                    });
                    if (meshComponent)
                    {
                        meshComponent->SetSourceMeshPath(std::string(assets::Project::kBuiltinCubeMeshReference));
                    }
                }
            }
            ImGui::EndPopup();
        }
    }
}