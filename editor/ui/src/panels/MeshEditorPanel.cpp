#include "PlutoGE/ui/panels/MeshEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_set>

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

        render::HumanoidBoneMapping *FindHumanoidMapping(render::Skeleton &skeleton, render::HumanoidBone bone)
        {
            for (auto &mapping : skeleton.humanoidBoneMappings)
            {
                if (mapping.bone == bone)
                    return &mapping;
            }
            return nullptr;
        }

        void EnsureHumanoidMappings(render::Skeleton &skeleton)
        {
            skeleton.humanoidBoneMappings.erase(
                std::remove_if(skeleton.humanoidBoneMappings.begin(), skeleton.humanoidBoneMappings.end(),
                               [](const render::HumanoidBoneMapping &mapping)
                               {
                                   return static_cast<std::size_t>(mapping.bone) >= render::kHumanoidBoneCount;
                               }),
                skeleton.humanoidBoneMappings.end());

            for (std::size_t index = 0; index < render::kHumanoidBoneCount; ++index)
            {
                const auto bone = static_cast<render::HumanoidBone>(index);
                if (!FindHumanoidMapping(skeleton, bone))
                {
                    skeleton.humanoidBoneMappings.push_back(render::HumanoidBoneMapping{
                        .bone = bone,
                        .copyTranslation = bone == render::HumanoidBone::Hips,
                    });
                }
            }
        }

        void AutoMapHumanoidRig(render::Skeleton &skeleton)
        {
            EnsureHumanoidMappings(skeleton);
            for (std::size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
            {
                const auto guessedBone = render::GuessHumanoidBone(skeleton.joints[jointIndex].name);
                if (!guessedBone)
                    continue;

                if (auto *mapping = FindHumanoidMapping(skeleton, *guessedBone); mapping && mapping->targetJointIndex < 0)
                {
                    mapping->targetJointIndex = static_cast<int>(jointIndex);
                }
            }
        }

        bool IsRequiredHumanoidBone(render::HumanoidBone bone)
        {
            switch (bone)
            {
            case render::HumanoidBone::Hips:
            case render::HumanoidBone::Spine:
            case render::HumanoidBone::Head:
            case render::HumanoidBone::LeftUpperArm:
            case render::HumanoidBone::LeftLowerArm:
            case render::HumanoidBone::LeftHand:
            case render::HumanoidBone::RightUpperArm:
            case render::HumanoidBone::RightLowerArm:
            case render::HumanoidBone::RightHand:
            case render::HumanoidBone::LeftUpperLeg:
            case render::HumanoidBone::LeftLowerLeg:
            case render::HumanoidBone::LeftFoot:
            case render::HumanoidBone::RightUpperLeg:
            case render::HumanoidBone::RightLowerLeg:
            case render::HumanoidBone::RightFoot:
                return true;
            default:
                return false;
            }
        }

        void DrawHumanoidRigEditor(render::Skeleton &skeleton, bool readOnly, bool &dirty)
        {
            ImGui::SeparatorText("Humanoid Rig");
            if (skeleton.joints.empty())
            {
                ImGui::TextDisabled("This mesh has no skeleton.");
                return;
            }

            if (skeleton.humanoidBoneMappings.empty())
            {
                ImGui::TextWrapped("Configure a humanoid map to retarget clips whose bone names or joint order differ from this mesh.");
                ImGui::BeginDisabled(readOnly);
                if (ImGui::Button("Configure and Auto Map"))
                {
                    AutoMapHumanoidRig(skeleton);
                    dirty = true;
                }
                ImGui::EndDisabled();
                return;
            }

            EnsureHumanoidMappings(skeleton);
            ImGui::BeginDisabled(readOnly);
            if (ImGui::Button("Auto Map Missing"))
            {
                AutoMapHumanoidRig(skeleton);
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable Retargeting"))
            {
                skeleton.humanoidBoneMappings.clear();
                dirty = true;
                ImGui::EndDisabled();
                return;
            }
            ImGui::EndDisabled();

            int requiredMapped = 0;
            int requiredCount = 0;
            bool duplicateTarget = false;
            std::unordered_set<int> assignedTargets;
            for (const auto &mapping : skeleton.humanoidBoneMappings)
            {
                if (IsRequiredHumanoidBone(mapping.bone))
                {
                    ++requiredCount;
                    if (mapping.targetJointIndex >= 0 && mapping.targetJointIndex < static_cast<int>(skeleton.joints.size()))
                        ++requiredMapped;
                }
                if (mapping.targetJointIndex >= 0 && !assignedTargets.insert(mapping.targetJointIndex).second)
                    duplicateTarget = true;
            }

            if (requiredMapped == requiredCount && !duplicateTarget)
                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Rig Ready (%d/%d required bones)", requiredMapped, requiredCount);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Needs Attention (%d/%d required bones)%s", requiredMapped, requiredCount, duplicateTarget ? " - duplicate target joints" : "");

            ImGui::TextWrapped("Source Override is optional. Set it when an animation uses a name the automatic Mixamo/Unreal/common aliases do not recognize.");
            ImGui::Columns(3, "HumanoidRigColumns", true);
            ImGui::TextUnformatted("Humanoid Bone"); ImGui::NextColumn();
            ImGui::TextUnformatted("Target Joint"); ImGui::NextColumn();
            ImGui::TextUnformatted("Source Override"); ImGui::NextColumn();
            ImGui::Separator();

            for (std::size_t boneIndex = 0; boneIndex < render::kHumanoidBoneCount; ++boneIndex)
            {
                const auto bone = static_cast<render::HumanoidBone>(boneIndex);
                auto *mapping = FindHumanoidMapping(skeleton, bone);
                if (!mapping)
                    continue;

                ImGui::PushID(static_cast<int>(boneIndex));
                ImGui::Text("%s%s", render::HumanoidBoneName(bone), IsRequiredHumanoidBone(bone) ? " *" : "");
                ImGui::NextColumn();

                const char *preview = mapping->targetJointIndex >= 0 && mapping->targetJointIndex < static_cast<int>(skeleton.joints.size())
                                          ? skeleton.joints[static_cast<std::size_t>(mapping->targetJointIndex)].name.c_str()
                                          : "Unassigned";
                ImGui::BeginDisabled(readOnly);
                if (ImGui::BeginCombo("##TargetJoint", preview))
                {
                    if (ImGui::Selectable("Unassigned", mapping->targetJointIndex < 0))
                    {
                        mapping->targetJointIndex = -1;
                        dirty = true;
                    }
                    for (std::size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
                    {
                        const bool selected = mapping->targetJointIndex == static_cast<int>(jointIndex);
                        if (ImGui::Selectable(skeleton.joints[jointIndex].name.c_str(), selected))
                        {
                            mapping->targetJointIndex = static_cast<int>(jointIndex);
                            dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                ImGui::NextColumn();

                char sourceName[128]{};
                strncpy_s(sourceName, mapping->sourceBoneName.c_str(), _TRUNCATE);
                ImGui::BeginDisabled(readOnly);
                if (ImGui::InputTextWithHint("##SourceName", "Auto aliases", sourceName, sizeof(sourceName)))
                {
                    mapping->sourceBoneName = sourceName;
                    dirty = true;
                }
                if (ImGui::DragFloat3("Rotation Offset", &mapping->rotationOffsetDegrees.x, 0.25f, -180.0f, 180.0f, "%.1f deg"))
                {
                    dirty = true;
                }
                if (ImGui::Checkbox("Copy Translation", &mapping->copyTranslation))
                {
                    dirty = true;
                }
                if (mapping->copyTranslation && ImGui::DragFloat("Translation Scale", &mapping->translationScale, 0.01f, 0.0f, 10.0f))
                {
                    dirty = true;
                }
                ImGui::EndDisabled();
                ImGui::NextColumn();
                ImGui::PopID();
            }
            ImGui::Columns(1);
            ImGui::TextDisabled("* Required bone");
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
                if (asset.type != assets::ProjectAssetType::Model)
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

            for (auto *meshComponent : entity.GetComponents<scene::MeshComponent>())
            {
                if (meshComponent && meshComponent->GetMeshAssetReference() == meshReference)
                {
                    meshComponent->SetMesh(mesh);
                    // SetMesh normally invalidates cached commands because a
                    // reloaded asset has a new pointer. Keep the refresh robust
                    // if asset loading later starts updating meshes in place.
                    meshComponent->NotifyMeshDataChanged();
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
        m_metadata = {};
        m_dirty = false;

        auto *mesh = core::Engine::GetInstance().GetAssetManager().LoadMeshAsset(reference);
        if (!mesh)
        {
            return;
        }

        m_config = BuildMeshConfigFromMesh(*mesh);
        EnsureBaseLodRanges(m_config);
        m_materialReferences = core::Engine::GetInstance().GetAssetManager().GetMeshAssetMaterialReferences(reference);
        m_metadata = core::Engine::GetInstance().GetAssetManager().GetMeshAssetMetadata(reference);
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
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string droppedReference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(droppedReference) == assets::ProjectAssetType::Material)
                        {
                            m_materialReferences[slotIndex] = droppedReference;
                            m_dirty = true;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (!m_materialReferences[slotIndex].empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Open"))
                {
                    editorShell.OpenMaterialAsset(m_materialReferences[slotIndex]);
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                {
                    m_materialReferences[slotIndex].clear();
                    m_dirty = true;
                }
            }
            ImGui::PopID();
        }

        DrawHumanoidRigEditor(m_config.skeleton, engineMesh, m_dirty);

        ImGui::SeparatorText("LOD Settings");
        auto *project = editorShell.GetProject();
        std::filesystem::path sourcePath;
        if (project && !engineMesh)
        {
            const auto resolvedSourcePath = editorShell.GetEngine().GetAssetManager().ResolveMeshAssetSourcePath(reference);
            sourcePath = resolvedSourcePath.empty() ? FindSourceModelForMesh(*project, reference) : std::filesystem::path(resolvedSourcePath);
        }
        const bool canReimport = project && !engineMesh && !sourcePath.empty();
        m_dirty |= ImGui::Checkbox("Generate LODs", &m_metadata.importOptions.generateLods);
        m_dirty |= ImGui::Checkbox("Optimize Vertex Cache", &m_metadata.importOptions.optimizeVertexCache);
        m_dirty |= ImGui::Checkbox("Optimize Overdraw", &m_metadata.importOptions.optimizeOverdraw);
        ImGui::BeginDisabled(!canReimport);
        if (ImGui::Button("Reimport With Settings"))
        {
            auto importedMeshAsset = editorShell.GetEngine().ImportMeshAsset(sourcePath.string(), m_metadata.importOptions);
            if (importedMeshAsset.mesh)
            {
                auto humanoidBoneMappings = std::move(m_config.skeleton.humanoidBoneMappings);
                m_config = BuildMeshConfigFromMesh(*importedMeshAsset.mesh);
                m_config.skeleton.humanoidBoneMappings = std::move(humanoidBoneMappings);
                EnsureBaseLodRanges(m_config);
                std::size_t requiredMaterialSlots = 0;
                for (const auto &submesh : m_config.submeshes)
                {
                    requiredMaterialSlots = (std::max)(requiredMaterialSlots, static_cast<std::size_t>(submesh.materialIndex) + 1);
                }
                m_materialReferences.resize(requiredMaterialSlots);
                if (m_metadata.sourceAssetReference.empty())
                {
                    m_metadata.sourceAssetReference = project->MakeAssetReference(sourcePath);
                }
                m_dirty = true;
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Reimported mesh asset with saved settings: " + reference);
            }
        }
        ImGui::EndDisabled();
        if (!canReimport)
        {
            ImGui::TextDisabled("Reimport needs a matching Source Model asset.");
        }

        const size_t maxLodCount = GetMaxLodCount(m_config);
        const std::uint64_t lod0Triangles = CountTrianglesForLod(m_config, 0);
        if (m_config.submeshes.size() > 500)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Large draw count: %zu submeshes (recommended maximum: 500).", m_config.submeshes.size());
        }
        if (lod0Triangles > 1'000'000)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Large LOD0: %llu triangles.", static_cast<unsigned long long>(lod0Triangles));
        }
        if (maxLodCount <= 1 && lod0Triangles > 100'000)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "This large mesh has no simplified LODs.");
        }
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
            if (editorShell.GetEngine().GetAssetManager().SaveMeshAsset(reference, m_config, m_materialReferences, &errorMessage, m_metadata))
            {
                // Extracted mesh copies retain the source object's stable identity.
                // Keep the model package's canonical mesh synchronized so edits
                // apply to every instance that inherits from that model object.
                std::string canonicalReference;
                if (!m_metadata.sourceAssetId.empty() && m_metadata.sourceObjectId != 0)
                {
                    canonicalReference = editorShell.GetEngine().GetAssetManager().ResolveModelObject(
                        m_metadata.sourceAssetId, m_metadata.sourceObjectId);
                }

                bool canonicalSaved = true;
                if (!canonicalReference.empty() && canonicalReference != reference)
                {
                    auto &assetManager = editorShell.GetEngine().GetAssetManager();
                    const auto canonicalMaterials = assetManager.GetMeshAssetMaterialReferences(canonicalReference);
                    const auto canonicalMetadata = assetManager.GetMeshAssetMetadata(canonicalReference);
                    canonicalSaved = assetManager.SaveMeshAsset(canonicalReference, m_config, canonicalMaterials,
                                                                 &errorMessage, canonicalMetadata);
                }

                m_dirty = false;
                LoadActiveMesh();
                RefreshOpenSceneMeshAssetInstances(editorShell.GetEngine(), reference);
                if (canonicalSaved && !canonicalReference.empty() && canonicalReference != reference)
                {
                    RefreshOpenSceneMeshAssetInstances(editorShell.GetEngine(), canonicalReference);
                }
                editorShell.MarkProjectDirty();
                editorShell.MarkSceneDirty();
                if (canonicalSaved)
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved mesh asset: " + reference);
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                    errorMessage.empty() ? "Saved the source mesh, but failed to update its imported scene asset."
                                                         : errorMessage);
                }
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
