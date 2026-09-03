#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"

// Editor selection access is validated by EditorShell before panel use.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
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
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kHierarchyDragDropPayload = "PLUTOGE_SCENE_ENTITY";

        bool DecomposeEntityTransform(const glm::mat4 &matrix, glm::vec3 &position, glm::vec3 &rotation, glm::vec3 &scale)
        {
            glm::quat orientation;
            glm::vec3 skew;
            glm::vec4 perspective;
            if (!glm::decompose(matrix, scale, orientation, position, skew, perspective))
            {
                return false;
            }
            orientation = glm::conjugate(orientation);
            rotation = glm::degrees(glm::eulerAngles(orientation));
            return true;
        }

        void SetLocalTransform(scene::Entity &entity, const glm::mat4 &matrix)
        {
            glm::vec3 position{0.0f};
            glm::vec3 rotation{0.0f};
            glm::vec3 scale{1.0f};
            if (DecomposeEntityTransform(matrix, position, rotation, scale))
            {
                entity.SetPosition(position);
                entity.SetRotation(rotation);
                entity.SetScale(scale);
            }
        }

        void SetWorldTransform(scene::Entity &entity, const glm::mat4 &worldTransform)
        {
            const glm::mat4 localTransform = entity.GetParent()
                                                 ? glm::inverse(entity.GetParent()->GetWorldTransform()) * worldTransform
                                                 : worldTransform;
            SetLocalTransform(entity, localTransform);
        }

        void AccumulateMeshBounds(scene::Entity *entity, glm::vec3 &minimum, glm::vec3 &maximum, bool &hasBounds)
        {
            if (!entity)
            {
                return;
            }
            if (auto *component = entity->GetComponent<scene::MeshComponent>(); component && component->GetMesh())
            {
                auto *mesh = component->GetMesh();
                const std::size_t begin = component->GetSubmeshIndex() >= 0
                                              ? static_cast<std::size_t>(component->GetSubmeshIndex())
                                              : 0;
                const std::size_t end = component->GetSubmeshIndex() >= 0
                                            ? std::min(begin + static_cast<std::size_t>(std::max(1, component->GetSubmeshRangeCount())), mesh->GetSubmeshCount())
                                            : mesh->GetSubmeshCount();
                for (std::size_t index = begin; index < end; ++index)
                {
                    const auto &submesh = mesh->GetSubmesh(index);
                    const glm::mat4 transform = entity->GetWorldTransform() * component->GetMeshOffsetTransform() *
                                                component->GetSubmeshOffsetTransform(index);
                    const auto &meshData = mesh->GetMeshData();
                    const std::size_t indexEnd = std::min<std::size_t>(submesh.indexOffset + submesh.indexCount, meshData.indices.size());
                    for (std::size_t meshIndex = submesh.indexOffset; meshIndex < indexEnd; ++meshIndex)
                    {
                        const auto vertexIndex = meshData.indices[meshIndex];
                        if (vertexIndex >= meshData.vertices.size())
                        {
                            continue;
                        }
                        const auto &position = meshData.vertices[vertexIndex].position;
                        const glm::vec3 worldPosition(transform * glm::vec4(position[0], position[1], position[2], 1.0f));
                        minimum = glm::min(minimum, worldPosition);
                        maximum = glm::max(maximum, worldPosition);
                        hasBounds = true;
                    }
                }
            }
            for (auto *child : entity->GetChildren())
            {
                AccumulateMeshBounds(child, minimum, maximum, hasBounds);
            }
        }

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

        scene::MeshComponent *FindSkeletonMeshComponent(scene::Entity *entity)
        {
            if (!entity)
            {
                return nullptr;
            }

            if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>();
                meshComponent && meshComponent->GetMesh() && meshComponent->GetMesh()->HasSkeleton())
            {
                return meshComponent;
            }

            for (auto *child : entity->GetChildren())
            {
                if (auto *meshComponent = FindSkeletonMeshComponent(child))
                {
                    return meshComponent;
                }
            }

            return nullptr;
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

        if (IsEntitySelected(entity))
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(static_cast<int>(entity->GetID()));
        bool nodeOpen = false;
        if (isRenaming)
        {
            nodeOpen = ImGui::TreeNodeEx("##RenameNode", nodeFlags);
            ImGui::SameLine();
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(availableWidth > 1.0f ? availableWidth : 1.0f);
            if (m_focusRenameInput)
            {
                ImGui::SetKeyboardFocusHere();
                m_focusRenameInput = false;
            }

            const bool submitted = ImGui::InputText("##RenameInput", m_renameBuffer.data(), m_renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool lostFocus = ImGui::IsItemDeactivated();
            if (escapePressed)
            {
                EndRename(false);
            }
            else if (submitted || lostFocus)
            {
                EndRename(true);
            }
        }
        else
        {
            nodeOpen = ImGui::TreeNodeEx("EntityNode", nodeFlags, "%s", entityName.c_str());
        }

        // If clicked, set this entity as the selected entity in the editor shell
        if (ImGui::IsItemClicked() && ImGui::IsItemHovered())
        {
            SelectEntity(entity, ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyCtrl);
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
                    if (!InstantiateModelAssetIntoScene(reference, entity))
                    {
                        InstantiateMeshAssetIntoScene(reference, entity);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Context menu for right-clicking on an entity
        if (ImGui::BeginPopupContextItem())
        {
            if (!IsEntitySelected(entity))
            {
                SelectEntity(entity, false, false);
            }
            ImGui::BeginDisabled(m_selectedEntityIds.size() < 2);
            if (ImGui::MenuItem("Group Selected", "Ctrl+G"))
            {
                m_groupSelectionRequested = true;
            }
            ImGui::EndDisabled();
            if (ImGui::BeginMenu("Set Pivot"))
            {
                if (ImGui::MenuItem("Bounds Center"))
                {
                    SetPivotToMeshBounds(entity, false);
                }
                if (ImGui::MenuItem("Bounds Bottom Center"))
                {
                    SetPivotToMeshBounds(entity, true);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
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
            if (FindSkeletonMeshComponent(entity))
            {
                if (ImGui::BeginMenu("Create Skeleton Attachment"))
                {
                    if (auto *meshComponent = FindSkeletonMeshComponent(entity))
                    {
                        const auto &joints = meshComponent->GetMesh()->GetSkeleton().joints;
                        for (std::size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
                        {
                            const auto &joint = joints[jointIndex];
                            const std::string label = joint.name.empty()
                                                          ? "Joint " + std::to_string(jointIndex)
                                                          : joint.name;
                            if (ImGui::MenuItem(label.c_str()))
                            {
                                EditorShell::GetInstance().ExecuteSceneEdit(
                                    "Create Skeleton Attachment",
                                    [meshComponent, jointIndex]()
                                    {
                                        if (auto *created = meshComponent->CreateSkeletonAttachmentEntity(jointIndex))
                                        {
                                            EditorShell::GetInstance().SetSelectedEntity(created);
                                        }
                                    });
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Compact Legacy Skeleton Attachments"))
                {
                    EditorShell::GetInstance().ExecuteSceneEdit(
                        "Compact Legacy Skeleton Attachments",
                        [entity]()
                        {
                            if (auto *meshComponent = FindSkeletonMeshComponent(entity))
                            {
                                meshComponent->CompactSkeletonAttachmentEntities();
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
                // Removing an entity here invalidates both `entity` below and
                // the parent/root vector currently being traversed. Defer the
                // edit until every hierarchy node has finished rendering.
                m_pendingDeleteEntityId = entity->GetID();
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
        if (ImGui::MenuItem("Ocean"))
        {
            CreatePresetEntity(EntityPreset::Ocean, parent);
        }
        if (ImGui::MenuItem("Terrain"))
        {
            CreatePresetEntity(EntityPreset::Terrain, parent);
        }
        if (ImGui::MenuItem("Cloth"))
        {
            CreatePresetEntity(EntityPreset::Cloth, parent);
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
        case EntityPreset::Ocean:
            editLabel = "Create Ocean";
            break;
        case EntityPreset::Terrain:
            editLabel = "Create Terrain";
            break;
        case EntityPreset::Cloth:
            editLabel = "Create Cloth";
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
                                                        case EntityPreset::Ocean:
                                                        {
                                                            auto *entity = addEntity("Ocean");
                                                            if (entity)
                                                            {
                                                                entity->CreateComponent<scene::OceanComponent>();
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
                                                        case EntityPreset::Cloth:
                                                        {
                                                            auto *entity = addEntity("Cloth");
                                                            if (entity)
                                                            {
                                                                entity->CreateComponent<scene::ClothComponent>();
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
            ImGui::EndDragDropTarget();
        }
    }

    bool SceneHierarchyPanel::IsEntitySelected(scene::Entity *entity) const
    {
        return entity && std::find(m_selectedEntityIds.begin(), m_selectedEntityIds.end(), entity->GetID()) != m_selectedEntityIds.end();
    }

    void SceneHierarchyPanel::SelectEntity(scene::Entity *entity, bool additive, bool rangeToggle)
    {
        if (!entity)
        {
            m_selectedEntityIds.clear();
            EditorShell::GetInstance().SetSelectedEntity(nullptr);
            return;
        }

        const auto found = std::find(m_selectedEntityIds.begin(), m_selectedEntityIds.end(), entity->GetID());
        if (!additive)
        {
            m_selectedEntityIds = {entity->GetID()};
        }
        else if (rangeToggle && found != m_selectedEntityIds.end())
        {
            m_selectedEntityIds.erase(found);
        }
        else if (found == m_selectedEntityIds.end())
        {
            m_selectedEntityIds.push_back(entity->GetID());
        }

        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        auto *primary = !m_selectedEntityIds.empty() && scene ? scene->FindEntityByID(m_selectedEntityIds.back()) : nullptr;
        EditorShell::GetInstance().SetSelectedEntity(primary);
    }

    void SceneHierarchyPanel::GroupSelectedEntities()
    {
        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        if (!scene || m_selectedEntityIds.size() < 2)
        {
            return;
        }

        std::vector<scene::Entity *> entities;
        entities.reserve(m_selectedEntityIds.size());
        for (const auto id : m_selectedEntityIds)
        {
            if (auto *entity = scene->FindEntityByID(id))
            {
                entities.push_back(entity);
            }
        }
        if (entities.size() < 2)
        {
            return;
        }

        auto *commonParent = entities.front()->GetParent();
        if (std::any_of(entities.begin(), entities.end(), [commonParent](const scene::Entity *entity)
                        { return entity->GetParent() != commonParent; }))
        {
            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning,
                                           "Group Selected requires all selected entities to have the same parent.");
            return;
        }

        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        bool hasBounds = false;
        for (auto *entity : entities)
        {
            AccumulateMeshBounds(entity, minimum, maximum, hasBounds);
        }
        const glm::vec3 pivot = hasBounds ? (minimum + maximum) * 0.5f : entities.front()->GetWorldPosition();

        scene::Entity *group = nullptr;
        EditorShell::GetInstance().ExecuteSceneEdit("Group Selected Entities",
                                                    [scene, commonParent, entities, pivot, &group]()
                                                    {
                                                        std::vector<glm::mat4> worldTransforms;
                                                        worldTransforms.reserve(entities.size());
                                                        for (auto *entity : entities)
                                                        {
                                                            worldTransforms.push_back(entity->GetWorldTransform());
                                                        }

                                                        auto groupEntity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "New Group"});
                                                        group = scene->AddEntity(std::move(groupEntity), commonParent);
                                                        if (!group)
                                                        {
                                                            return;
                                                        }
                                                        group->SetWorldPosition(pivot);
                                                        for (std::size_t index = 0; index < entities.size(); ++index)
                                                        {
                                                            entities[index]->SetParent(group);
                                                            SetWorldTransform(*entities[index], worldTransforms[index]);
                                                        }
                                                    });
        if (group)
        {
            m_selectedEntityIds = {group->GetID()};
            EditorShell::GetInstance().SetSelectedEntity(group);
            BeginRename(group);
        }
    }

    void SceneHierarchyPanel::SetPivotToMeshBounds(scene::Entity *entity, bool bottomCenter)
    {
        if (!entity)
        {
            return;
        }
        if (entity->GetComponent<scene::MeshComponent>())
        {
            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning,
                                           "Set Pivot currently applies to group entities. Group mesh entities first so geometry can be preserved.");
            return;
        }

        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        bool hasBounds = false;
        AccumulateMeshBounds(entity, minimum, maximum, hasBounds);
        if (!hasBounds)
        {
            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning, "No mesh bounds were found below this entity.");
            return;
        }

        glm::vec3 pivot = (minimum + maximum) * 0.5f;
        if (bottomCenter)
        {
            pivot.y = minimum.y;
        }
        auto children = entity->GetChildren();
        EditorShell::GetInstance().ExecuteSceneEdit(bottomCenter ? "Set Pivot To Bounds Bottom" : "Set Pivot To Bounds Center",
                                                    [entity, children, pivot]()
                                                    {
                                                        std::vector<glm::mat4> childWorldTransforms;
                                                        childWorldTransforms.reserve(children.size());
                                                        for (auto *child : children)
                                                        {
                                                            childWorldTransforms.push_back(child->GetWorldTransform());
                                                        }
                                                        entity->SetWorldPosition(pivot);
                                                        for (std::size_t index = 0; index < children.size(); ++index)
                                                        {
                                                            SetWorldTransform(*children[index], childWorldTransforms[index]);
                                                        }
                                                    });
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

        auto *shellSelection = EditorShell::GetInstance().GetSelectedEntity();
        if (shellSelection && !IsEntitySelected(shellSelection))
        {
            m_selectedEntityIds = {shellSelection->GetID()};
        }
        else if (!shellSelection && !m_selectedEntityIds.empty())
        {
            m_selectedEntityIds.clear();
        }

        m_selectedEntityIds.erase(std::remove_if(m_selectedEntityIds.begin(), m_selectedEntityIds.end(),
                                                  [scene](std::uint32_t id) { return scene->FindEntityByID(id) == nullptr; }),
                                  m_selectedEntityIds.end());
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false) && !ImGui::GetIO().WantTextInput)
        {
            GroupSelectedEntities();
        }

        for (auto entity : scene->GetRootEntities())
        {
            RenderEntityNode(entity);
        }

        if (m_pendingDeleteEntityId != 0)
        {
            const auto deletedEntityId = m_pendingDeleteEntityId;
            m_pendingDeleteEntityId = 0;
            if (auto *entity = scene->FindEntityByID(deletedEntityId))
            {
                EditorShell::GetInstance().SetSelectedEntity(entity);
                if (EditorShell::GetInstance().DeleteSelectedEntity())
                {
                    m_selectedEntityIds.erase(
                        std::remove(m_selectedEntityIds.begin(), m_selectedEntityIds.end(), deletedEntityId),
                        m_selectedEntityIds.end());
                    if (m_renamingEntityId == deletedEntityId)
                    {
                        EndRename(false);
                    }
                }
            }
        }

        if (m_groupSelectionRequested)
        {
            m_groupSelectionRequested = false;
            GroupSelectedEntities();
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered())
        {
            SelectEntity(nullptr, false, false);
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
