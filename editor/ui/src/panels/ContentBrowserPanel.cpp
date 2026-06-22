#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        bool ContainsInsensitive(std::string_view text, std::string_view filter)
        {
            if (filter.empty())
            {
                return true;
            }

            auto lower = [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            };

            std::string haystack(text);
            std::string needle(filter);
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
            std::transform(needle.begin(), needle.end(), needle.begin(), lower);
            return haystack.find(needle) != std::string::npos;
        }

        std::string DisplayAssetReference(std::string reference)
        {
            if (reference.rfind(assets::Project::kProjectAssetScheme, 0) == 0)
            {
                reference.erase(0, assets::Project::kProjectAssetScheme.size());
            }
            return reference;
        }

        std::string SanitizeAssetFileName(std::string_view text)
        {
            std::string name;
            name.reserve(text.size());
            for (const char rawCharacter : text)
            {
                const unsigned char character = static_cast<unsigned char>(rawCharacter);
                if (std::isalnum(character) != 0 || rawCharacter == '_' || rawCharacter == '-')
                {
                    name.push_back(rawCharacter);
                }
                else if (!name.empty() && name.back() != '_')
                {
                    name.push_back('_');
                }
            }

            while (!name.empty() && name.back() == '_')
            {
                name.pop_back();
            }
            return name;
        }

        std::string BuildEntityNameForMeshReference(const std::string &reference)
        {
            std::string displayName = DisplayAssetReference(reference);
            if (displayName.rfind("engine://", 0) == 0)
            {
                const auto separator = displayName.find_last_of('/');
                return separator == std::string::npos ? displayName : displayName.substr(separator + 1);
            }

            std::filesystem::path path(displayName);
            const auto stem = path.stem().string();
            return stem.empty() ? displayName : stem;
        }

        bool IsSupportedImportedModelReference(const std::string &reference)
        {
            const auto extension = std::filesystem::path(reference).extension().string();
            return extension == ".gltf" || extension == ".glb" || extension == ".fbx" ||
                   extension == ".GLTF" || extension == ".GLB" || extension == ".FBX";
        }

        void AttachImportedAnimations(scene::Entity &entity,
                                      const std::string &sourceReference,
                                      const core::ImportedRenderMeshAsset &importedMeshAsset)
        {
            if (!importedMeshAsset.animations || importedMeshAsset.animations->empty())
            {
                return;
            }

            auto *animationComponent = entity.GetComponent<scene::AnimationComponent>();
            if (!animationComponent)
            {
                animationComponent = entity.CreateComponent<scene::AnimationComponent>();
            }

            animationComponent->SetClipsFromImportedAnimations(*importedMeshAsset.animations);
            animationComponent->SetSourceAnimationPath(sourceReference);
        }

        render::Mesh *GetOrCreateIsolatedSubmeshRuntimeMesh(const std::string &reference,
                                                            int submeshIndex,
                                                            int submeshCount,
                                                            const render::Mesh &sourceMesh);

        bool ConfigureMeshComponentForReference(scene::Entity &entity,
                                                const std::string &reference,
                                                int submeshIndex,
                                                int submeshCount,
                                                int materialSlot,
                                                std::string *errorMessage)
        {
            auto &engine = core::Engine::GetInstance();
            auto *meshComponent = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});

            if (assets::Project::IsEngineAssetReference(reference))
            {
                auto *mesh = engine.GetAssetManager().LoadMeshAsset(reference);
                if (!mesh)
                {
                    if (errorMessage)
                    {
                        *errorMessage = "Failed to load mesh asset: " + reference;
                    }
                    return false;
                }

                meshComponent->SetMesh(mesh);
                meshComponent->SetSourceMeshPath(reference);
                meshComponent->SetMaterialForMaterialSlot(
                    0,
                    engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
                meshComponent->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                if (submeshIndex >= 0)
                {
                    meshComponent->SetSubmeshRange(submeshIndex, submeshCount);
                    if (static_cast<size_t>(submeshIndex) < mesh->GetSubmeshCount())
                    {
                        meshComponent->SetMaterialForSubmesh(
                            static_cast<size_t>(submeshIndex),
                            engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
                        meshComponent->SetMaterialAssetForSubmesh(static_cast<size_t>(submeshIndex),
                                                                  std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                    }
                }
                else
                {
                    meshComponent->CreateSubmeshChildEntities();
                }
                return true;
            }

            const std::string resolvedPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(reference);
            if (!engine.GetMeshImporter().SupportsFileType(resolvedPath))
            {
                if (errorMessage)
                {
                    *errorMessage = "Unsupported mesh format for model subassets: " + reference;
                }
                return false;
            }

            auto importedMeshAsset = engine.ImportMeshAsset(resolvedPath);
            if (!importedMeshAsset.mesh)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to import mesh asset: " + reference;
                }
                return false;
            }

            meshComponent->SetMesh(importedMeshAsset.mesh);
            meshComponent->SetMaterials(importedMeshAsset.materials);
            meshComponent->SetSourceMeshPath(reference);
            if (submeshIndex >= 0)
            {
                if (static_cast<size_t>(submeshIndex) < importedMeshAsset.mesh->GetSubmeshCount())
                {
                    const auto &submesh = importedMeshAsset.mesh->GetSubmesh(static_cast<size_t>(submeshIndex));
                    const uint32_t resolvedMaterialSlot = materialSlot >= 0
                                                              ? static_cast<uint32_t>(materialSlot)
                                                              : submesh.materialIndex;
                    if (resolvedMaterialSlot < importedMeshAsset.materials.size())
                    {
                        if (auto *isolatedMesh = GetOrCreateIsolatedSubmeshRuntimeMesh(reference, submeshIndex, submeshCount, *importedMeshAsset.mesh))
                        {
                            meshComponent->SetMesh(isolatedMesh);
                        }
                        meshComponent->SetMaterials(importedMeshAsset.materials);
                        meshComponent->SetMaterialForSubmesh(static_cast<size_t>(submeshIndex),
                                                             importedMeshAsset.materials[resolvedMaterialSlot]);
                    }
                    meshComponent->SetSubmeshRange(submeshIndex, submeshCount);
                    if (!submesh.name.empty())
                    {
                        entity.SetName(submesh.name);
                    }
                }
            }
            else
            {
                meshComponent->CreateSubmeshChildEntities();
            }

            AttachImportedAnimations(entity, reference, importedMeshAsset);
            return true;
        }

        render::Mesh *GetOrCreateIsolatedSubmeshRuntimeMesh(const std::string &reference,
                                                            int submeshIndex,
                                                            int submeshCount,
                                                            const render::Mesh &sourceMesh)
        {
            if (submeshIndex < 0 || static_cast<size_t>(submeshIndex) >= sourceMesh.GetSubmeshCount())
            {
                return nullptr;
            }

            static std::unordered_map<std::string, std::unique_ptr<render::Mesh>> isolatedMeshes;
            const int normalizedCount = std::max(1, submeshCount);
            const size_t submeshEnd = std::min(static_cast<size_t>(submeshIndex + normalizedCount), sourceMesh.GetSubmeshCount());
            const std::string key = reference + "#submesh:" + std::to_string(submeshIndex) + "+" + std::to_string(submeshEnd - static_cast<size_t>(submeshIndex));
            const auto cached = isolatedMeshes.find(key);
            if (cached != isolatedMeshes.end())
            {
                return cached->second.get();
            }

            std::vector<render::Submesh> submeshes(submeshEnd);
            for (size_t index = static_cast<size_t>(submeshIndex); index < submeshEnd; ++index)
            {
                submeshes[index] = sourceMesh.GetSubmesh(index);
            }

            render::MeshConfig config;
            config.data = sourceMesh.GetMeshData();
            config.submeshes = std::move(submeshes);
            config.hasLightmapUvs = sourceMesh.HasLightmapUvs();
            auto mesh = std::unique_ptr<render::Mesh>(render::Mesh::FromConfig(std::move(config)));
            auto *meshPtr = mesh.get();
            isolatedMeshes.emplace(key, std::move(mesh));
            return meshPtr;
        }
    }

    bool InstantiateMeshAssetIntoScene(std::string reference, scene::Entity *parent, int submeshIndex, int submeshCount, int materialSlot)
    {
        if (reference.empty() || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Mesh)
        {
            return false;
        }

        auto &editorShell = EditorShell::GetInstance();
        auto *scene = editorShell.GetEngine().GetScene();
        if (!scene)
        {
            return false;
        }

        scene::Entity *createdEntity = nullptr;
        std::string errorMessage;
        editorShell.ExecuteSceneEdit(submeshIndex >= 0 ? "Instantiate Mesh Subasset" : "Instantiate Mesh Asset",
                                     [scene, parent, reference, submeshIndex, submeshCount, materialSlot, &createdEntity, &errorMessage]()
                                     {
                                         auto entity = std::make_unique<scene::Entity>(scene::EntityConfig{
                                             .name = BuildEntityNameForMeshReference(reference),
                                         });
                                         createdEntity = scene->AddEntity(std::move(entity), parent);
                                         if (!createdEntity || !ConfigureMeshComponentForReference(*createdEntity, reference, submeshIndex, submeshCount, materialSlot, &errorMessage))
                                         {
                                             if (createdEntity)
                                             {
                                                 scene->RemoveEntity(createdEntity);
                                                 createdEntity = nullptr;
                                             }
                                             return;
                                         }
                                     });

        if (createdEntity)
        {
            editorShell.SetSelectedEntity(createdEntity);
            editorShell.MarkSceneDirty();
            return true;
        }

        if (!errorMessage.empty())
        {
            editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
        }
        return false;
    }

    void ContentBrowserPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        if (!project)
        {
            ImGui::TextDisabled("No project loaded.");
            return;
        }

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("Filter", m_filterBuffer.data(), m_filterBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            project->RefreshAssetRegistry();
            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Refreshed project assets.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Material"))
        {
            m_newMaterialNameBuffer.fill('\0');
            ImGui::OpenPopup("Create Material Asset");
        }

        if (ImGui::BeginPopupModal("Create Material Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newMaterialNameBuffer.data(), m_newMaterialNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newMaterialNameBuffer.data());
            if (!sanitizedName.empty())
            {
                ImGui::TextDisabled("Creates Materials/%s.plutomaterial", sanitizedName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a material name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto materialPath = project->GetAssetDirectoryPath() / "Materials" / (sanitizedName + ".plutomaterial");
                const std::string reference = project->MakeAssetReference(materialPath);
                render::MaterialConfig config;
                config.color = glm::vec4(0.82f, 0.84f, 0.88f, 1.0f);
                config.metallic = 0.0f;
                config.roughness = 0.55f;

                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    editorShell.OpenMaterialAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created material: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create material." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::TextDisabled("Assets: %zu", project->GetManifest().assetEntries.size());
        ImGui::Separator();

        const auto &assets = project->GetManifest().assetEntries;
        ImGui::BeginChild("Assets", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 3.0f), true);
        for (int index = 0; index < static_cast<int>(assets.size()); ++index)
        {
            const auto &asset = assets[static_cast<std::size_t>(index)];
            const std::string displayName = std::string("[") + std::string(assets::Project::GetAssetTypeName(asset.type)) + "] " + DisplayAssetReference(asset.reference);
            if (!ContainsInsensitive(displayName, m_filterBuffer.data()))
            {
                continue;
            }

            const bool selected = m_selectedAssetIndex == index;
            const bool canExpandMesh = asset.type == assets::ProjectAssetType::Mesh &&
                                       !assets::Project::IsEngineAssetReference(asset.reference) &&
                                       IsSupportedImportedModelReference(asset.reference);
            bool rowActivated = false;
            bool treeOpen = false;
            ImGui::PushID(index);
            if (canExpandMesh)
            {
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                 ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                 ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 (selected ? ImGuiTreeNodeFlags_Selected : 0);
                treeOpen = ImGui::TreeNodeEx("AssetTreeNode", flags, "%s", displayName.c_str());
                rowActivated = ImGui::IsItemClicked();
            }
            else
            {
                rowActivated = ImGui::Selectable(displayName.c_str(), selected);
            }

            if (rowActivated)
            {
                m_selectedAssetIndex = index;
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                const auto resolvedPath = project->ResolveAssetReference(asset.reference);
                if (resolvedPath.extension() == ".plutoscene")
                {
                    editorShell.OpenSceneFromPath(resolvedPath);
                }
                else if (resolvedPath.extension() == ".plutoprefab")
                {
                    editorShell.OpenSceneFromPath(resolvedPath);
                }
                else if (asset.type == assets::ProjectAssetType::Material)
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
            }

            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kContentBrowserAssetDragDropPayload,
                                          asset.reference.c_str(),
                                          asset.reference.size() + 1);
                ImGui::TextUnformatted(displayName.c_str());
                ImGui::EndDragDropSource();
            }

            if (canExpandMesh && treeOpen)
            {
                try
                {
                    const std::string resolvedPath = editorShell.GetEngine().GetAssetManager().ResolveMeshAssetSourcePath(asset.reference);
                    auto importedMeshAsset = editorShell.GetEngine().ImportMeshAsset(resolvedPath);
                    auto *mesh = importedMeshAsset.mesh;
                    if (!mesh || mesh->GetSubmeshCount() == 0)
                    {
                        ImGui::TextDisabled("No mesh children.");
                    }
                    else
                    {
                        for (size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshCount();)
                        {
                            const auto &submesh = mesh->GetSubmesh(submeshIndex);
                            const std::string submeshName = submesh.name.empty()
                                                                ? std::string("Mesh ") + std::to_string(submeshIndex)
                                                                : submesh.name;
                            size_t groupEnd = submeshIndex + 1;
                            while (groupEnd < mesh->GetSubmeshCount())
                            {
                                const auto &nextSubmesh = mesh->GetSubmesh(groupEnd);
                                const std::string nextName = nextSubmesh.name.empty()
                                                                 ? std::string("Mesh ") + std::to_string(groupEnd)
                                                                 : nextSubmesh.name;
                                if (nextName != submeshName)
                                {
                                    break;
                                }
                                ++groupEnd;
                            }

                            std::string slotSummary;
                            uint32_t indexCount = 0;
                            for (size_t groupedIndex = submeshIndex; groupedIndex < groupEnd; ++groupedIndex)
                            {
                                const auto &groupedSubmesh = mesh->GetSubmesh(groupedIndex);
                                if (!slotSummary.empty())
                                {
                                    slotSummary += ", ";
                                }
                                slotSummary += std::to_string(groupedSubmesh.materialIndex);
                                indexCount += groupedSubmesh.indexCount;
                            }

                            const int groupCount = static_cast<int>(groupEnd - submeshIndex);
                            const std::string submeshDisplayName = submeshName +
                                                                   (groupCount > 1 ? " [" + std::to_string(groupCount) + " parts, slots " + slotSummary + "]"
                                                                                   : " [Slot " + slotSummary + "]");
                            ImGui::PushID(static_cast<int>(submeshIndex));
                            ImGui::Selectable(submeshDisplayName.c_str(), false);
                            if (ImGui::BeginDragDropSource())
                            {
                                ContentBrowserMeshSubassetPayload payload{};
                                strncpy_s(payload.sourceReference, asset.reference.c_str(), _TRUNCATE);
                                payload.submeshIndex = static_cast<int>(submeshIndex);
                                payload.submeshCount = groupCount;
                                payload.materialSlot = static_cast<int>(submesh.materialIndex);
                                ImGui::SetDragDropPayload(kContentBrowserMeshSubassetDragDropPayload,
                                                          &payload,
                                                          sizeof(payload));
                                ImGui::Text("%s", submeshDisplayName.c_str());
                                ImGui::TextDisabled("Material slots %s, %u indices", slotSummary.c_str(), indexCount);
                                ImGui::EndDragDropSource();
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("slots %s, %u indices", slotSummary.c_str(), indexCount);
                            ImGui::PopID();
                            submeshIndex = groupEnd;
                        }
                    }
                }
                catch (const std::exception &exception)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", exception.what());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (m_selectedAssetIndex >= 0 && m_selectedAssetIndex < static_cast<int>(assets.size()))
        {
            const auto &asset = assets[static_cast<std::size_t>(m_selectedAssetIndex)];
            const auto resolvedPath = project->ResolveAssetReference(asset.reference);
            const std::string typeName(assets::Project::GetAssetTypeName(asset.type));
            ImGui::Text("Type: %s", typeName.c_str());
            ImGui::TextWrapped("Reference: %s", asset.reference.c_str());
            if (assets::Project::IsEngineAssetReference(asset.reference))
            {
                ImGui::TextWrapped("Engine Asset");
            }
            else
            {
                ImGui::TextWrapped("Path: %s", resolvedPath.string().c_str());
            }

            if (asset.type == assets::ProjectAssetType::Material)
            {
                if (ImGui::Button("Open Material"))
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
            }
        }
    }
}
