#include "PlutoGE/ui/panels/MeshEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <filesystem>
#include <limits>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        render::MeshConfig BuildMeshConfigFromMesh(const render::Mesh &mesh)
        {
            render::MeshConfig config;
            config.data = mesh.GetMeshData();
            config.hasLightmapUvs = mesh.HasLightmapUvs();
            config.skeleton = mesh.GetSkeleton();
            config.animationNodes = mesh.GetAnimationNodes();
            config.animations = mesh.GetAnimations();
            config.submeshes.reserve(mesh.GetSubmeshCount());
            for (size_t submeshIndex = 0; submeshIndex < mesh.GetSubmeshCount(); ++submeshIndex)
            {
                config.submeshes.push_back(mesh.GetSubmesh(submeshIndex));
            }
            return config;
        }

        size_t GetMaxLodCount(const render::MeshConfig &config)
        {
            size_t maxLodCount = 1;
            for (const auto &submesh : config.submeshes)
            {
                maxLodCount = (std::max)(maxLodCount, (std::max<size_t>)(submesh.lods.size(), 1));
            }
            return maxLodCount;
        }

        uint64_t CountTrianglesForLod(const render::MeshConfig &config, size_t lodIndex)
        {
            uint64_t indexCount = 0;
            for (const auto &submesh : config.submeshes)
            {
                if (lodIndex < submesh.lods.size())
                {
                    indexCount += submesh.lods[lodIndex].indexCount;
                }
                else if (lodIndex == 0)
                {
                    indexCount += submesh.indexCount;
                }
            }
            return indexCount / 3;
        }

        void EnsureBaseLodRanges(render::MeshConfig &config)
        {
            for (auto &submesh : config.submeshes)
            {
                if (!submesh.lods.empty())
                {
                    continue;
                }

                submesh.lods.push_back(render::Submesh::LodRange{
                    .indexOffset = submesh.indexOffset,
                    .indexCount = submesh.indexCount,
                    .minDistanceFactor = 0.0f,
                    .maxScreenRadiusPixels = std::numeric_limits<float>::max(),
                });
            }
        }

        bool ApplyLodThreshold(render::MeshConfig &config, size_t lodIndex, float maxScreenRadiusPixels)
        {
            bool changed = false;
            for (auto &submesh : config.submeshes)
            {
                if (lodIndex >= submesh.lods.size())
                {
                    continue;
                }

                const float sanitized = lodIndex == 0
                                            ? std::numeric_limits<float>::max()
                                            : (std::max)(1.0f, maxScreenRadiusPixels);
                if (submesh.lods[lodIndex].maxScreenRadiusPixels != sanitized)
                {
                    submesh.lods[lodIndex].maxScreenRadiusPixels = sanitized;
                    changed = true;
                }
            }
            return changed;
        }

        std::filesystem::path FindSourceModelForMesh(const assets::Project &project, const std::string &meshReference)
        {
            const auto meshPath = project.ResolveAssetReference(meshReference);
            const auto meshStem = meshPath.stem().string();
            for (const auto &asset : project.GetManifest().assetEntries)
            {
                if (asset.type != assets::ProjectAssetType::SourceModel)
                {
                    continue;
                }

                const auto sourcePath = project.ResolveAssetReference(asset.reference);
                if (sourcePath.stem() == meshPath.stem())
                {
                    return sourcePath;
                }
            }

            const auto guessedSourceDirectory = project.GetAssetDirectoryPath() / "SourceModels" / meshStem;
            constexpr const char *extensions[] = {".gltf", ".glb", ".fbx"};
            for (const auto *extension : extensions)
            {
                const auto candidate = guessedSourceDirectory / (meshStem + extension);
                if (std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }
            return {};
        }

        void RefreshMeshAssetInstances(scene::Entity &entity, const std::string &meshReference, render::Mesh *mesh)
        {
            if (!mesh)
            {
                return;
            }

            if (auto *meshComponent = entity.GetComponent<scene::MeshComponent>())
            {
                if (meshComponent->GetMeshAssetReference() == meshReference)
                {
                    meshComponent->SetMesh(mesh);
                }
            }

            for (auto *child : entity.GetChildren())
            {
                if (child)
                {
                    RefreshMeshAssetInstances(*child, meshReference, mesh);
                }
            }
        }

        void RefreshOpenSceneMeshAssetInstances(core::Engine &engine, const std::string &meshReference)
        {
            auto *scene = engine.GetScene();
            if (!scene)
            {
                return;
            }

            auto *mesh = engine.GetAssetManager().LoadMeshAsset(meshReference);
            for (auto *root : scene->GetRootEntities())
            {
                if (root)
                {
                    RefreshMeshAssetInstances(*root, meshReference, mesh);
                }
            }
            scene->MarkShadowLightsDirty();
        }
    }

    void MeshEditorPanel::LoadActiveMesh()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveMeshAssetReference();
        m_loadedReference = reference;
        m_config = {};
        m_materialReferences.clear();
        m_dirty = false;

        auto *mesh = core::Engine::GetInstance().GetAssetManager().LoadMeshAsset(reference);
        if (!mesh)
        {
            return;
        }

        m_config = BuildMeshConfigFromMesh(*mesh);
        EnsureBaseLodRanges(m_config);
        m_materialReferences = core::Engine::GetInstance().GetAssetManager().GetMeshAssetMaterialReferences(reference);
    }

    void MeshEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveMeshAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No mesh selected.");
            return;
        }

        if (reference != m_loadedReference)
        {
            LoadActiveMesh();
        }

        const bool engineMesh = assets::Project::IsEngineAssetReference(reference);
        ImGui::TextWrapped("Mesh: %s", reference.c_str());
        if (engineMesh)
        {
            ImGui::TextDisabled("Engine mesh assets are read-only.");
        }

        if (m_config.data.vertices.empty() || m_config.data.indices.empty())
        {
            ImGui::TextDisabled("Mesh asset could not be loaded.");
            return;
        }

        ImGui::SeparatorText("Preview");
        const auto *mesh = core::Engine::GetInstance().GetAssetManager().LoadMeshAsset(reference);
        if (mesh)
        {
            const auto &bounds = mesh->GetBounds();
            ImGui::Text("Vertices: %zu", mesh->GetVertexCount());
            ImGui::Text("Triangles: %zu", mesh->GetIndexCount() / 3);
            ImGui::Text("Submeshes: %zu", mesh->GetSubmeshCount());
            ImGui::Text("LOD Levels: %zu", GetMaxLodCount(m_config));
            ImGui::Text("Bounds Center: %.2f, %.2f, %.2f", bounds.center.x, bounds.center.y, bounds.center.z);
            ImGui::Text("Bounds Radius: %.2f", bounds.radius);
            ImGui::TextDisabled("Rendered orbit preview can be layered onto this panel next.");
        }

        ImGui::SeparatorText("Materials");
        if (m_materialReferences.empty())
        {
            ImGui::TextDisabled("No material slots saved on this mesh asset.");
        }
        for (size_t slotIndex = 0; slotIndex < m_materialReferences.size(); ++slotIndex)
        {
            ImGui::PushID(static_cast<int>(slotIndex));
            ImGui::Text("Slot %zu", slotIndex);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", m_materialReferences[slotIndex].empty() ? "(empty)" : m_materialReferences[slotIndex].c_str());
            if (!m_materialReferences[slotIndex].empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Open"))
                {
                    editorShell.OpenMaterialAsset(m_materialReferences[slotIndex]);
                }
            }
            ImGui::PopID();
        }

        ImGui::SeparatorText("LOD Settings");
        auto *project = editorShell.GetProject();
        const bool canGenerateLods = project && !engineMesh && !FindSourceModelForMesh(*project, reference).empty();
        ImGui::BeginDisabled(!canGenerateLods);
        if (ImGui::Button("Generate LODs From Source Model"))
        {
            const auto sourcePath = FindSourceModelForMesh(*project, reference);
            auto importedMeshAsset = editorShell.GetEngine().GenerateMeshAssetLods(sourcePath.string());
            if (importedMeshAsset.mesh)
            {
                m_config = BuildMeshConfigFromMesh(*importedMeshAsset.mesh);
                EnsureBaseLodRanges(m_config);
                m_dirty = true;
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Generated LODs for mesh asset: " + reference);
            }
        }
        ImGui::EndDisabled();
        if (!canGenerateLods)
        {
            ImGui::TextDisabled("LOD generation needs a matching Source Model asset.");
        }

        const size_t maxLodCount = GetMaxLodCount(m_config);
        for (size_t lodIndex = 0; lodIndex < maxLodCount; ++lodIndex)
        {
            ImGui::PushID(static_cast<int>(lodIndex));
            const uint64_t triangles = CountTrianglesForLod(m_config, lodIndex);
            if (lodIndex == 0)
            {
                ImGui::Text("LOD 0: %llu triangles", static_cast<unsigned long long>(triangles));
                ImGui::TextDisabled("LOD 0 is used for the nearest/full-detail view.");
            }
            else
            {
                float threshold = 0.0f;
                for (const auto &submesh : m_config.submeshes)
                {
                    if (lodIndex < submesh.lods.size())
                    {
                        threshold = submesh.lods[lodIndex].maxScreenRadiusPixels;
                        break;
                    }
                }

                ImGui::Text("LOD %zu: %llu triangles", lodIndex, static_cast<unsigned long long>(triangles));
                if (ImGui::DragFloat("Max Screen Radius", &threshold, 1.0f, 1.0f, 2000.0f, "%.0f px"))
                {
                    m_dirty |= ApplyLodThreshold(m_config, lodIndex, threshold);
                }
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::BeginDisabled(engineMesh || !m_dirty);
        if (ImGui::Button("Save Mesh Asset"))
        {
            std::string errorMessage;
            if (editorShell.GetEngine().GetAssetManager().SaveMeshAsset(reference, m_config, m_materialReferences, &errorMessage))
            {
                m_dirty = false;
                LoadActiveMesh();
                RefreshOpenSceneMeshAssetInstances(editorShell.GetEngine(), reference);
                editorShell.MarkProjectDirty();
                editorShell.MarkSceneDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved mesh asset: " + reference);
            }
            else
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save mesh asset." : errorMessage);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Revert"))
        {
            LoadActiveMesh();
        }
        ImGui::EndDisabled();
    }
}
