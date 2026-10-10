#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/ModelInstanceRefresh.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/ModelInstanceUnpacking.h"
#include "PlutoGE/asset_import/ModelGenerationExtraction.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include <imgui.h>
#include <utility>

namespace PlutoGE::ui
{
    void EditorShell::RecordModelInstanceRefresh(const PreparedModelInstanceRefresh &prepared)
    {
        m_modelInstanceConflicts = prepared.conflicts;
        m_modelInstanceSourceDiagnostics = prepared.diagnostics;
        m_modelInstanceRefreshError.clear();
        std::vector<std::string> messages;
        for (const auto &diagnostic : prepared.diagnostics)
            messages.push_back("Linked model " + diagnostic.sourceAssetId + ": " + diagnostic.message +
                " Accepted instances have been retained.");
        for (const auto &conflict : prepared.conflicts)
            messages.push_back("Linked model instance " + std::to_string(conflict.rootEntityId) +
                " has conflicting source changes. Its whole accepted generation has been retained.");
        if (messages != m_modelInstanceMessages)
            for (const auto &message : messages) Log(ConsoleSeverity::Warning, message);
        m_modelInstanceMessages = std::move(messages);
    }
    void EditorShell::PollModelInstances()
    {
        if (!m_project || !m_scene || m_project->GetManifest().assetPipelineVersion < 5 ||
            m_engine.IsRuntimeRunning() || m_sceneEditInProgress || m_activeBakeTask) return;
        if (m_requestedModelInstanceUnpack)
        {
            const auto root = std::exchange(m_requestedModelInstanceUnpack, 0);
            std::string error;
            if (!UnpackModelInstance(root, error))
            {
                m_modelInstanceUnpackErrorRoot=root;
                m_modelInstanceUnpackError=std::move(error);
                Log(ConsoleSeverity::Error, m_modelInstanceUnpackError);
            }
            return;
        }
        if (!m_modelInstancesNeedRefresh) return;
        FlushUntrackedSceneEdit();
        const auto revision = m_sceneRevision;
        PreparedModelInstanceRefresh prepared;
        std::string error;
        if (!PrepareModelInstanceRefresh(*m_scene, *m_project, m_engine.GetAssetManager(), prepared, &error))
        {
            m_modelInstanceRefreshError = std::move(error);
            return; // Retry a competing/interrupted publication without replacing live state.
        }
        if (revision != m_sceneRevision) return;
        if (prepared.scene)
        {
            const auto selected = m_entitySelection.Ids();
            SetScene(std::move(prepared.scene), false);
            SetSelectedEntities(selected);
            MarkSceneDirty();
            SynchronizeHistoryState();
        }
        m_modelInstancesNeedRefresh = false;
        RecordModelInstanceRefresh(prepared);
    }
    bool EditorShell::UnpackModelInstance(std::uint32_t rootEntityId, std::string &error)
    {
        if (!m_project || !m_scene || m_engine.IsRuntimeRunning() || m_sceneEditInProgress || m_activeBakeTask || IsModelImportRunning())
        { error="Finish the active edit/import or stop Play before unpacking."; return false; }
        const auto found=m_scene->GetStaticModelInstances().find(rootEntityId);
        if (found == m_scene->GetStaticModelInstances().end()) { error="Selected linked instance no longer exists."; return false; }
        FlushUntrackedSceneEdit();
        const auto revision=m_sceneRevision;
        const auto state=found->second.state;
        const auto destination="project://Snapshots/Unpacked_" + assets::GenerateAssetId();
        assetimport::ModelGenerationExtractionResult extracted;
        if (!assetimport::ExtractStaticModelGeneration(*m_project, state, destination, extracted, &error)) return false;
        // Files are authored assets and remain available when the scene edit is undone.
        m_assetRefreshPending=true;
        const auto retained=[&]() { error += " Authored bundle remains at " + destination; return false; };
        if (!assetimport::VerifyModelGenerationExtraction(*m_project, extracted, &error)) return retained();
        auto &assets=m_engine.GetAssetManager();
        assets.SetAssetSnapshot(extracted.catalog, extracted.storage);
        std::unique_ptr<scene::Scene> candidate;
        if (!scene::PrepareStaticModelInstanceUnpacking(*m_scene, rootEntityId, extracted.references, assets, candidate, &error) ||
            !assetimport::VerifyModelGenerationExtraction(*m_project, extracted, &error)) return retained();
        if (revision != m_sceneRevision) { error="Scene changed during unpack preparation."; return retained(); }
        const auto selected=m_entitySelection.Ids();
        ExecuteSceneEdit("Unpack Model Instance", [&]
        {
            SetScene(std::move(candidate), false);
            SetSelectedEntities(selected);
        });
        Log(ConsoleSeverity::Info, "Unpacked model instance. Authored bundle: " + destination);
        error.clear();
        return true;
    }

    void EditorShell::RenderModelInstanceInspector(scene::Entity &entity)
    {
        if (!m_scene) return;
        const auto &instances = m_scene->GetStaticModelInstances();
        auto found = instances.end();
        for (auto *ancestor = &entity; ancestor && found == instances.end(); ancestor = ancestor->GetParent())
            found = instances.find(ancestor->GetID());
        if (found == instances.end()) return;
        ImGui::TextUnformatted("Linked Model Instance");
        ImGui::TextWrapped("Source: %s", found->second.state.accepted.layout.sourceAssetId.c_str());
        ImGui::TextDisabled("Accepted generation: %.12s", content::DigestToHex(found->second.state.artifactGenerationKey).c_str());
        for (const auto &diagnostic : m_modelInstanceSourceDiagnostics)
            if (std::find(diagnostic.roots.begin(), diagnostic.roots.end(), found->first) != diagnostic.roots.end())
                ImGui::TextWrapped("Accepted instance retained: %s", diagnostic.message.c_str());
        for (const auto &conflict : m_modelInstanceConflicts) if (conflict.rootEntityId == found->first)
        {
            ImGui::TextWrapped("Source changes conflict with authored edits. The whole accepted instance is retained.");
            for (const auto &reason : conflict.reasons)
            {
                const char *message = "Source changes require review.";
                using Kind = assets::StaticModelInstanceConflictKind;
                switch (reason.kind)
                {
                case Kind::SourceMeshChanged: message = "Source geometry changed while generated geometry has authored edits."; break;
                case Kind::EditedNodeRemoved: message = "The source removed an edited node."; break;
                case Kind::EditedSubtreeReparented: message = "The source moved an edited subtree."; break;
                case Kind::MaterialBindingChanged: message = "The source changed an overridden material binding."; break;
                case Kind::InstanceStructureEdited: message = "Generated nodes have authored hierarchy changes."; break;
                case Kind::GeometryBindingChanged: message = "The source changed bindings on edited geometry."; break;
                case Kind::OverrideDependencyRemoved: message = "The source removed an asset used by an authored override."; break;
                }
                ImGui::BulletText("%s", message);
            }
        }
        if (!m_modelInstanceRefreshError.empty()) ImGui::TextWrapped("Refresh pending: %s", m_modelInstanceRefreshError.c_str());
        if (m_modelInstanceUnpackErrorRoot == found->first && !m_modelInstanceUnpackError.empty())
            ImGui::TextWrapped("Unpack failed: %s", m_modelInstanceUnpackError.c_str());
        ImGui::BeginDisabled(m_engine.IsRuntimeRunning() || m_sceneEditInProgress || bool(m_activeBakeTask) || IsModelImportRunning());
        if (ImGui::Button("Retry Source Update")) m_modelInstancesNeedRefresh = true;
        ImGui::SameLine();
        if (ImGui::Button("Unpack Instance")) m_requestedModelInstanceUnpack=found->first;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep this accepted hierarchy and its edits as ordinary authored objects.\nShared mesh/material/texture copies are created in Assets/Snapshots.\nScene undo restores linkage; created assets remain.");
        ImGui::EndDisabled();
        ImGui::Separator();
    }

}
