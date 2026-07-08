#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"

// Editor selection access is validated by EditorShell before panel use.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include <imgui.h>

#include <cctype>
#include <cstring>
#include <filesystem>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kHierarchyDragDropPayload = "PLUTOGE_SCENE_ENTITY";

        bool SceneHasAnyCamera(scene::Entity *entity)
        {
            if (!entity)
            {
                return false;
            }

            if (entity->HasComponent<scene::CameraComponent>())
            {
                return true;
            }

            for (auto *child : entity->GetChildren())
            {
                if (SceneHasAnyCamera(child))
                {
                    return true;
                }
            }

            return false;
        }

        bool SceneHasAnyCamera(scene::Scene *scene)
        {
            if (!scene)
            {
                return false;
            }

            for (auto *rootEntity : scene->GetRootEntities())
            {
                if (SceneHasAnyCamera(rootEntity))
                {
                    return true;
                }
            }

            return false;
        }

        std::string SanitizePrefabFileName(std::string text)
        {
            for (auto &character : text)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) == 0 && character != '_' && character != '-')
                {
                    character = '_';
                }
            }
            if (text.empty())
            {
                text = "Prefab";
            }
            return text;
        }

        bool InstantiatePrefabIntoScene(std::string reference, scene::Entity *parent)
        {
            auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
            if (!scene || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Prefab)
            {
                return false;
            }

            std::string errorMessage;
            scene::Entity *createdEntity = nullptr;
            EditorShell::GetInstance().ExecuteSceneEdit("Instantiate Prefab",
                                                        [scene, parent, reference = std::move(reference), &createdEntity, &errorMessage]()
                                                        {
                                                            createdEntity = scene::Prefab::Instantiate(*scene, reference, parent, &errorMessage);
                                                        });
            if (createdEntity)
            {
                EditorShell::GetInstance().SetSelectedEntity(createdEntity);
                return true;
            }

            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error,
                                           errorMessage.empty() ? "Failed to instantiate prefab." : errorMessage);
            return false;
        }

        void LinkPrefabSubtree(scene::Entity *entity, const std::string &prefabReference, bool isRoot)
        {
            if (!entity)
            {
                return;
            }

            entity->SetPrefabLink(prefabReference, entity->GetID(), isRoot);
            entity->ClearPrefabOverrides();
            for (auto *child : entity->GetChildren())
            {
                LinkPrefabSubtree(child, prefabReference, false);
            }
        }

    }

    void SceneHierarchyPanel::RenderEntityNode(scene::Entity *entity)
    {
        if (!entity)
        {
            return;
        }

        const std::string entityName = entity->GetName().empty() ? "Entity" : entity->GetName();
        const bool isRenaming = m_renamingEntityId == entity->GetID();
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (entity->GetChildren().empty())
        {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf;
        }

        if (EditorShell::GetInstance().GetSelectedEntity() == entity)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(static_cast<int>(entity->GetID()));
        bool nodeOpen = false;
        if (isRenaming)
        {
            nodeOpen = ImGui::TreeNodeEx("##RenameNode", nodeFlags);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            if (m_focusRenameInput)
            {
                ImGui::SetKeyboardFocusHere();
                m_focusRenameInput = false;
            }

            const bool submitted = ImGui::InputText("##RenameInput", m_renameBuffer.data(), m_renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool escapePressed = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
            if (submitted || deactivatedAfterEdit)
            {
                EndRename(true);
            }
            else if (escapePressed)
            {
                EndRename(false);
            }
        }
        else
        {
            nodeOpen = ImGui::TreeNodeEx("EntityNode", nodeFlags, "%s", entityName.c_str());
        }

        // If clicked, set this entity as the selected entity in the editor shell
        if (ImGui::IsItemClicked() && ImGui::IsItemHovered())
        {
            EditorShell::GetInstance().SetSelectedEntity(entity);
        }

        if (ImGui::BeginDragDropSource())
        {
            auto *draggedEntity = entity;
            ImGui::SetDragDropPayload(kHierarchyDragDropPayload, &draggedEntity, sizeof(draggedEntity));
            ImGui::TextUnformatted(entityName.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayload))
            {
                auto *draggedEntity = *static_cast<scene::Entity *const *>(payload->Data);
                if (draggedEntity && draggedEntity != entity)
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Reparent Entity",
                                                                [draggedEntity, entity]()
                                                                {
                                                                    draggedEntity->SetParent(entity);
                                                                });
                }
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
            {
                const std::string reference(static_cast<const char *>(payload->Data), payload->DataSize > 0 ? payload->DataSize - 1 : 0);
                if (!InstantiatePrefabIntoScene(reference, entity))
                {
                    InstantiateMeshAssetIntoScene(reference, entity);
                }
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserMeshSubassetDragDropPayload))
            {
                const auto *meshPayload = static_cast<const ContentBrowserMeshSubassetPayload *>(payload->Data);
                if (meshPayload)
                {
                    InstantiateMeshAssetIntoScene(meshPayload->sourceReference, entity, meshPayload->submeshIndex, meshPayload->submeshCount, meshPayload->materialSlot);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Context menu for right-clicking on an entity
        if (ImGui::BeginPopupContextItem())
        {
            EditorShell::GetInstance().SetSelectedEntity(entity);
            if (ImGui::MenuItem("Rename"))
            {
                BeginRename(entity);
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C"))
            {
                EditorShell::GetInstance().CopySelectedEntity();
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            {
                EditorShell::GetInstance().DuplicateSelectedEntity();
            }
            if (ImGui::BeginMenu("Create Child"))
            {
                RenderCreateMenu(entity);
                ImGui::EndMenu();
            }
            if (entity->GetParent() && ImGui::MenuItem("Unparent"))
            {
                EditorShell::GetInstance().ExecuteSceneEdit("Unparent Entity",
                                                            [entity]()
                                                            {
                                                                entity->SetParent(nullptr);
                                                            });
            }
            if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>();
                meshComponent && meshComponent->GetMesh() && meshComponent->GetMesh()->GetSubmeshCount() > 1)
            {
                if (ImGui::MenuItem("Create Submesh Entities"))
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Create Submesh Entities",
                                                                [entity]()
                                                                {
                                                                    if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
                                                                    {
                                                                        meshComponent->CreateSubmeshChildEntities();
                                                                    }
                    });
                }
            }
            if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>();
                meshComponent && meshComponent->GetMesh() && meshComponent->GetMesh()->HasSkeleton())
            {
                if (ImGui::MenuItem("Create Skeleton Attachment Entities"))
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Create Skeleton Attachment Entities",
                                                                [entity]()
                                                                {
                                                                    if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
                                                                    {
                                                                        meshComponent->CreateSkeletonAttachmentEntities();
                                                                    }
                                                                });
                }
            }
            if (ImGui::MenuItem("Save As Prefab"))
            {
                if (auto *project = EditorShell::GetInstance().GetProject())
                {
                    const auto prefabDirectory = project->GetAssetDirectoryPath() / "Prefabs";
                    const auto prefabName = SanitizePrefabFileName(entity->GetName().empty() ? "Prefab" : entity->GetName());
                    const auto prefabPath = prefabDirectory / (prefabName + ".plutoprefab");
                    const std::string prefabReference = project->MakeAssetReference(prefabPath);
                    std::string errorMessage;
                    if (scene::Prefab::SaveFromEntity(*entity, prefabPath, &errorMessage))
                    {
                        LinkPrefabSubtree(entity, prefabReference, true);
                        project->RefreshAssetRegistry();
                        EditorShell::GetInstance().MarkSceneDirty();
                        EditorShell::GetInstance().MarkProjectDirty();
                        EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Info, "Saved prefab: " + prefabReference);
                    }
                    else
                    {
                        EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error,
                                                       errorMessage.empty() ? "Failed to save prefab." : errorMessage);
                    }
                }
            }
            if (ImGui::MenuItem("Delete Entity", "Del"))
            {
                const scene::EntityID deletedEntityId = entity->GetID();
                if (EditorShell::GetInstance().DeleteSelectedEntity())
                {
                    if (m_renamingEntityId == deletedEntityId)
                    {
                        EndRename(false);
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

    void SceneHierarchyPanel::RenderCreateMenu(scene::Entity *parent)
    {
        if (ImGui::MenuItem("Empty Entity"))
        {
            CreatePresetEntity(EntityPreset::Empty, parent);
        }
        if (ImGui::MenuItem("Cube"))
        {
            CreatePresetEntity(EntityPreset::Cube, parent);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Camera"))
        {
            CreatePresetEntity(EntityPreset::Camera, parent);
        }
        if (ImGui::MenuItem("Directional Light"))
        {
            CreatePresetEntity(EntityPreset::DirectionalLight, parent);
        }
        if (ImGui::MenuItem("Point Light"))
        {
            CreatePresetEntity(EntityPreset::PointLight, parent);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Sky"))
        {
            CreatePresetEntity(EntityPreset::Sky, parent);
        }
        if (ImGui::MenuItem("Terrain"))
        {
            CreatePresetEntity(EntityPreset::Terrain, parent);
        }
        if (ImGui::MenuItem("Particle System"))
        {
            CreatePresetEntity(EntityPreset::ParticleSystem, parent);
        }
        if (ImGui::MenuItem("IBL Capture"))
        {
            CreatePresetEntity(EntityPreset::IblCapture, parent);
        }
    }

    void SceneHierarchyPanel::CreatePresetEntity(EntityPreset preset, scene::Entity *parent)
    {
        auto &engine = core::Engine::GetInstance();
        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        if (!scene)
        {
            return;
        }

        scene::Entity *createdEntity = nullptr;
        const bool sceneAlreadyHasCamera = SceneHasAnyCamera(scene);
        const char *editLabel = "Create Entity";
        switch (preset)
        {
        case EntityPreset::Empty:
            editLabel = "Create Empty Entity";
            break;
        case EntityPreset::Cube:
            editLabel = "Create Cube";
            break;
        case EntityPreset::Camera:
            editLabel = "Create Camera";
            break;
        case EntityPreset::DirectionalLight:
            editLabel = "Create Directional Light";
            break;
        case EntityPreset::PointLight:
            editLabel = "Create Point Light";
            break;
        case EntityPreset::Sky:
            editLabel = "Create Sky";
            break;
        case EntityPreset::Terrain:
            editLabel = "Create Terrain";
            break;
        case EntityPreset::ParticleSystem:
            editLabel = "Create Particle System";
            break;
        case EntityPreset::IblCapture:
            editLabel = "Create IBL Capture";
            break;
        }

        EditorShell::GetInstance().ExecuteSceneEdit(editLabel,
                                                    [&engine, scene, parent, preset, sceneAlreadyHasCamera, &createdEntity]()
                                                    {
                                                        auto addEntity = [scene, parent, &createdEntity](std::string name)
                                                        {
                                                            auto newEntity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = std::move(name)});
                                                            createdEntity = scene->AddEntity(std::move(newEntity), parent);
                                                            return createdEntity;
                                                        };

                                                        switch (preset)
                                                        {
                                                        case EntityPreset::Empty:
                                                            addEntity("New Entity");
                                                            break;
                                                        case EntityPreset::Cube:
                                                        {
                                                            auto *entity = addEntity("Cube");
                                                            if (!entity)
                                                            {
                                                                break;
                                                            }
                                                            auto *meshComponent = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                                                                .mesh = engine.GetAssetManager().LoadMeshAsset(std::string(assets::Project::kBuiltinCubeMeshReference)),
                                                                .material = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                                                            });
                                                            if (meshComponent)
                                                            {
                                                                meshComponent->SetSourceMeshPath(std::string(assets::Project::kBuiltinCubeMeshReference));
                                                                meshComponent->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                                                            }
                                                            break;
                                                        }
                                                        case EntityPreset::Camera:
                                                        {
                                                            auto *entity = addEntity("Camera");
                                                            if (!entity)
                                                            {
                                                                break;
                                                            }
                                                            entity->SetPosition(glm::vec3(0.0f, 2.0f, 5.0f));
                                                            entity->SetRotation(glm::vec3(-15.0f, 0.0f, 0.0f));
                                                            auto *cameraComponent = entity->CreateComponent<scene::CameraComponent>(new render::Camera(render::CameraConfig{
                                                                .fovY = 60.0f,
                                                                .nearPlane = 0.1f,
                                                                .farPlane = 100.0f,
                                                            }));
                                                            if (cameraComponent)
                                                            {
                                                                cameraComponent->SetMainCamera(!sceneAlreadyHasCamera);
                                                            }
                                                            break;
                                                        }
                                                        case EntityPreset::DirectionalLight:
                                                        {
                                                            auto *entity = addEntity("Directional Light");
                                                            if (auto *lightComponent = entity ? entity->CreateComponent<scene::LightComponent>() : nullptr)
                                                            {
                                                                lightComponent->SetLightType(scene::LightType::Directional);
                                                                lightComponent->SetIntensity(4.0f);
                                                                lightComponent->SetDirection(glm::normalize(glm::vec3(-0.45f, -0.85f, -0.25f)));
                                                                lightComponent->SetCastsShadows(true);
                                                            }
                                                            break;
                                                        }
                                                        case EntityPreset::PointLight:
                                                        {
                                                            auto *entity = addEntity("Point Light");
                                                            if (!entity)
                                                            {
                                                                break;
                                                            }
                                                            entity->SetPosition(glm::vec3(0.0f, 2.0f, 0.0f));
                                                            if (auto *lightComponent = entity->CreateComponent<scene::LightComponent>())
                                                            {
                                                                lightComponent->SetLightType(scene::LightType::Point);
                                                                lightComponent->SetIntensity(8.0f);
                                                                lightComponent->SetRange(12.0f);
                                                            }
                                                            break;
                                                        }
                                                        case EntityPreset::Sky:
                                                        {
                                                            auto *entity = addEntity("Sky");
                                                            if (entity)
                                                            {
                                                                entity->CreateComponent<scene::PhysicalSkyComponent>();
                                                                entity->CreateComponent<scene::VolumetricCloudComponent>();
                                                            }
                                                            break;
                                                        }
                                                        case EntityPreset::Terrain:
                                                        {
                                                            auto *entity = addEntity("Terrain");
                                                            if (entity)
                                                            {
                                                                auto *terrainComponent = entity->CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{
                                                                    .material = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                                                                });
                                                                if (terrainComponent)
                                                                {
                                                                    terrainComponent->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                                                                }
                                                            }
                                                            break;
                                                        }
                                                        case EntityPreset::ParticleSystem:
                                                        {
                                                            auto *entity = addEntity("Particle System");
                                                            if (entity)
                                                            {
                                                                entity->CreateComponent<scene::ParticleSystemComponent>();
                                                            }
                                                            break;
                                                        }
                                                        case EntityPreset::IblCapture:
                                                        {
                                                            auto *entity = addEntity("IBL Capture");
                                                            if (entity)
                                                            {
                                                                entity->CreateComponent<scene::IblCaptureComponent>();
                                                            }
                                                            break;
                                                        }
                                                        }
                                                    });

        if (createdEntity)
        {
            EditorShell::GetInstance().SetSelectedEntity(createdEntity);
            BeginRename(createdEntity);
        }
    }

    void SceneHierarchyPanel::BeginRename(scene::Entity *entity)
    {
        if (!entity)
        {
            return;
        }

        m_renamingEntityId = entity->GetID();
        m_focusRenameInput = true;
        std::memset(m_renameBuffer.data(), 0, m_renameBuffer.size());
        const std::string name = entity->GetName();
        const std::size_t copyLength = std::min(name.size(), m_renameBuffer.size() - 1);
        std::memcpy(m_renameBuffer.data(), name.c_str(), copyLength);
    }

    void SceneHierarchyPanel::EndRename(bool applyChanges)
    {
        if (applyChanges && m_renamingEntityId != 0)
        {
            auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
            if (scene)
            {
                if (auto *entity = scene->FindEntityByID(m_renamingEntityId))
                {
                    const scene::EntityID entityId = m_renamingEntityId;
                    std::string newName = m_renameBuffer.data();
                    EditorShell::GetInstance().ExecuteSceneEdit("Rename Entity",
                                                                [scene, entityId, newName = std::move(newName)]()
                                                                {
                                                                    if (auto *target = scene->FindEntityByID(entityId))
                                                                    {
                                                                        target->SetName(newName);
                                                                    }
                                                                });
                }
            }
        }

        m_renamingEntityId = 0;
        m_focusRenameInput = false;
        std::memset(m_renameBuffer.data(), 0, m_renameBuffer.size());
    }

    void SceneHierarchyPanel::RenderRootDropTarget()
    {
        ImGui::Selectable("Scene Root##HierarchyRootTarget", false, ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayload))
            {
                auto *draggedEntity = *static_cast<scene::Entity *const *>(payload->Data);
                if (draggedEntity)
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Move Entity To Root",
                                                                [draggedEntity]()
                                                                {
                                                                    draggedEntity->SetParent(nullptr);
                                                                });
                }
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
            {
                const std::string reference(static_cast<const char *>(payload->Data), payload->DataSize > 0 ? payload->DataSize - 1 : 0);
                if (!InstantiatePrefabIntoScene(reference, nullptr))
                {
                    InstantiateMeshAssetIntoScene(reference, nullptr);
                }
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserMeshSubassetDragDropPayload))
            {
                const auto *meshPayload = static_cast<const ContentBrowserMeshSubassetPayload *>(payload->Data);
                if (meshPayload)
                {
                    InstantiateMeshAssetIntoScene(meshPayload->sourceReference, nullptr, meshPayload->submeshIndex, meshPayload->submeshCount, meshPayload->materialSlot);
                }
            }
            ImGui::EndDragDropTarget();
        }
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
        RenderRootDropTarget();
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
            ImGui::BeginDisabled(!EditorShell::GetInstance().HasCopiedEntity());
            if (ImGui::MenuItem("Paste", "Ctrl+V"))
            {
                EditorShell::GetInstance().PasteCopiedEntity();
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::BeginMenu("Create"))
            {
                RenderCreateMenu(nullptr);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
}
