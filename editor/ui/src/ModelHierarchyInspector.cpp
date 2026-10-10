#include "PlutoGE/ui/ModelHierarchyInspector.h"
#include "PlutoGE/ui/ModelHierarchyView.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/scene/Scene.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <optional>
#include <unordered_set>

namespace PlutoGE::ui
{
    namespace
    {
        const char *IdentityStatus(assets::ModelNodeIdentityStatus status)
        {
            using Status = assets::ModelNodeIdentityStatus;
            switch (status)
            {
            case Status::Matched: return "Resolved";
            case Status::Anonymous: return "Unresolved: unnamed source node";
            case Status::DuplicateSiblingName: return "Unresolved: duplicate sibling names";
            case Status::DuplicateSourceIdentifier: return "Unresolved: duplicate source identifier";
            case Status::UnresolvedAncestor: return "Unresolved: ancestor identity is ambiguous";
            }
            return "Unresolved";
        }
        void Matrix(const char *label, const glm::mat4 &matrix)
        {
            if (!ImGui::TreeNode(label)) return;
            if (ImGui::BeginTable("Matrix", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Borders))
            {
                for (int row = 0; row < 4; ++row)
                {
                    ImGui::TableNextRow();
                    for (int column = 0; column < 4; ++column)
                    {
                        ImGui::TableSetColumnIndex(column);
                        ImGui::Text("%.6g", static_cast<double>(matrix[column][row]));
                    }
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }

    struct ModelHierarchyInspector::State
    {
        std::string reference, key, error;
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::optional<ModelHierarchyView> view;
        std::array<char, 160> search{};
        std::vector<unsigned char> expanded;
        std::vector<ModelHierarchyRow> rows;
        bool selectedOnly = true, wasImportRunning = false, rowsDirty = true;
        int selectedNode = -1;
        int repairSelectionNode = -1;
        std::uint64_t repairTarget = 0;
        std::optional<assetimport::ModelNodeRepairProposal> repair;
        std::string repairMessage;
        std::vector<content::ContentDigest> repairGenerations;
        bool repairTargetsBuilt = false;
        std::vector<std::pair<std::uint64_t, std::string>> repairTargets;
    };

    ModelHierarchyInspector::ModelHierarchyInspector() : m_state(std::make_unique<State>()) {}
    ModelHierarchyInspector::~ModelHierarchyInspector() = default;

    void ModelHierarchyInspector::Render(const assets::Project &project, const std::string &reference,
        const assets::ModelAsset &package, std::shared_ptr<const assets::AssetCatalog> catalog, bool importRunning,
        const scene::Scene *scene,
        const std::function<bool(const assetimport::ModelNodeRepairProposal &, std::string *)> &applyRepair)
    {
        if (!ImGui::CollapsingHeader("Source Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::PushID("SourceHierarchy");
        if (project.GetManifest().assetPipelineVersion < 3)
        {
            ImGui::TextWrapped("Source hierarchy inspection is available in Library-based projects (version 3 or later).");
            ImGui::PopID();
            return;
        }
        auto &state = *m_state;
        if (state.reference != reference)
        {
            state = {};
            state.reference = reference;
        }
        assets::ModelHierarchyArtifact descriptor;
        const auto status = assets::ReadModelHierarchyArtifact(package, descriptor);
        const auto key = project.GetManifestPath().generic_string() + ":" + package.sourceAssetId + ":" + std::to_string(static_cast<int>(status)) + ":" +
            content::DigestToHex(descriptor.digest);
        ImGui::BeginDisabled(importRunning);
        const bool refresh = ImGui::SmallButton("Refresh Hierarchy");
        ImGui::EndDisabled();
        if (importRunning) ImGui::TextWrapped("Import in progress. Showing the last inspected hierarchy until import completes.");
        if (!importRunning && (refresh || state.key != key || state.catalog != catalog || state.wasImportRunning))
        {
            std::uint64_t selectedId = 0;
            std::unordered_set<std::uint64_t> expandedIds;
            if (state.view && state.view->GetAsset().sourceAssetId == package.sourceAssetId)
            {
                const auto &identities = state.view->GetAsset().identities;
                if (state.selectedNode >= 0) selectedId = identities[state.selectedNode].localId;
                for (std::size_t index = 0; index < state.expanded.size(); ++index)
                    if (state.expanded[index] && identities[index].localId) expandedIds.insert(identities[index].localId);
            }
            state.repair.reset();
            state.repairTargetsBuilt = false;
            state.repairTargets.clear();
            state.repairTarget = 0;
            state.repairMessage.clear();
            state.key = key;
            state.catalog = std::move(catalog);
            state.view.reset();
            state.selectedNode = -1;
            state.error.clear();
            state.rowsDirty = true;
            assets::ModelHierarchyAsset snapshot;
            ModelHierarchyView view;
            if (assets::LoadModelHierarchyAsset(project, reference, snapshot, &state.error) && view.Reset(std::move(snapshot), &state.error))
            {
                state.expanded.assign(view.GetAsset().hierarchy.nodes.size(), 0);
                for (int index = 0; index < static_cast<int>(state.expanded.size()); ++index)
                {
                    if (view.GetAsset().hierarchy.nodes[index].parentNodeIndex < 0 ||
                        expandedIds.contains(view.GetAsset().identities[index].localId)) state.expanded[index] = 1;
                    if (selectedId && view.GetAsset().identities[index].localId == selectedId) state.selectedNode = index;
                }
                for (const int root : view.GetAsset().hierarchy.sceneRoots) state.expanded[root] = 1;
                if (state.selectedNode >= 0)
                    for (int parent = view.GetAsset().hierarchy.nodes[state.selectedNode].parentNodeIndex; parent >= 0;
                        parent = view.GetAsset().hierarchy.nodes[parent].parentNodeIndex) state.expanded[parent] = 1;
                state.view = std::move(view);
            }
        }
        state.wasImportRunning = importRunning;
        if (!state.error.empty()) ImGui::TextWrapped("Hierarchy unavailable: %s", state.error.c_str());
        if (!state.view) { ImGui::PopID(); return; }
        const auto &view = *state.view;
        const auto &asset = view.GetAsset();
        ImGui::Text("Selected scene: %zu nodes, %zu bindings", view.SelectedNodeCount(), view.SelectedBindingCount());
        ImGui::Text("Source inventory: %zu nodes, %zu bindings", asset.hierarchy.nodes.size(), asset.hierarchy.bindings.size());
        if (view.UnresolvedSelectedNodeCount())
            ImGui::TextWrapped("%zu selected node%s need%s identity repair before linked instantiation.",
                view.UnresolvedSelectedNodeCount(), view.UnresolvedSelectedNodeCount() == 1 ? "" : "s",
                view.UnresolvedSelectedNodeCount() == 1 ? "s" : "");
        if (!view.GetStaticTransformError().empty())
            ImGui::TextWrapped("Static transform preparation: %s", view.GetStaticTransformError().c_str());
        else
            ImGui::TextDisabled("Selected bindings support static transform preparation.");
        state.rowsDirty |= ImGui::Checkbox("Selected scene only", &state.selectedOnly);
        ImGui::SetNextItemWidth(-1);
        state.rowsDirty |= ImGui::InputTextWithHint("##NodeSearch", "Search node names", state.search.data(), state.search.size());
        if (ImGui::SmallButton("Expand All"))
        { std::fill(state.expanded.begin(), state.expanded.end(), 1); state.rowsDirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Collapse All"))
        { std::fill(state.expanded.begin(), state.expanded.end(), 0); state.rowsDirty = true; }
        if (state.rowsDirty)
        {
            state.rows = view.Rows(state.selectedOnly, state.search.data(), state.expanded);
            state.rowsDirty = false;
        }
        // Fixed-height rows can be clipped even for a very large or deep model.
        ImGui::BeginChild("NodeTree", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12), true);
        if (state.rows.empty()) ImGui::TextDisabled("%s", state.search[0] ? "No matching nodes." : "No nodes in this source scene.");
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.rows.size()), ImGui::GetTextLineHeightWithSpacing());
        while (clipper.Step())
            for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
            {
                const auto &row = state.rows[rowIndex];
                const auto &node = asset.hierarchy.nodes[row.nodeIndex];
                const auto &identity = asset.identities[row.nodeIndex];
                ImGui::PushID(row.nodeIndex);
                const float indent = std::min(row.depth, 12u) * ImGui::GetFontSize();
                if (indent > 0) ImGui::Indent(indent);
                auto flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (!row.hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
                if (state.selectedNode == row.nodeIndex) flags |= ImGuiTreeNodeFlags_Selected;
                ImGui::SetNextItemOpen(state.search[0] || state.expanded[row.nodeIndex], ImGuiCond_Always);
                ImGui::TreeNodeEx("Node", flags, "%s%s", node.name.empty() ? "(unnamed)" : node.name.c_str(),
                    identity.localId ? "" : "  [unresolved]");
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) state.selectedNode = row.nodeIndex;
                if (ImGui::IsItemToggledOpen() && !state.search[0])
                { state.expanded[row.nodeIndex] = !state.expanded[row.nodeIndex]; state.rowsDirty = true; }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\nSource depth: %u\nBindings: %zu", IdentityStatus(identity.status), row.depth, view.GetBindings()[row.nodeIndex].size());
                if (indent > 0) ImGui::Unindent(indent);
                ImGui::PopID();
            }
        ImGui::EndChild();
        if (state.selectedNode >= 0)
        {
            const int index = state.selectedNode;
            const auto &node = asset.hierarchy.nodes[index];
            const auto &identity = asset.identities[index];
            ImGui::SeparatorText("Source Node");
            ImGui::TextWrapped("Name: %s", node.name.empty() ? "(unnamed)" : node.name.c_str());
            ImGui::TextUnformatted(IdentityStatus(identity.status));
            ImGui::Text("In selected scene: %s", view.IsSelectedNode(index) ? "Yes" : "No");
            if (identity.localId)
            {
                const auto id = std::to_string(identity.localId);
                ImGui::TextWrapped("Source node ID: %s", id.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy ID")) ImGui::SetClipboardText(id.c_str());
                ImGui::TextWrapped("Source key: %s", identity.sourceKey.c_str());
            }
            if (node.parentNodeIndex >= 0)
                ImGui::TextWrapped("Source parent: %s", asset.hierarchy.nodes[node.parentNodeIndex].name.c_str());
            else ImGui::TextDisabled("Source root");
            if (project.GetManifest().assetPipelineVersion >= 5 && scene && applyRepair)
            {
                if (state.repairSelectionNode != index)
                {
                    state.repairSelectionNode = index;
                    state.repairTarget = 0;
                    state.repair.reset();
                    state.repairMessage.clear();
                }
                if (!identity.localId)
                    ImGui::TextWrapped("Give this source node a unique authored name or persistent producer ID before repairing correspondence.");
                else if (ImGui::TreeNode("Repair Correspondence"))
                {
                    auto &targets = state.repairTargets;
                    std::vector<content::ContentDigest> generations;
                    for (const auto &[root, instance] : scene->GetStaticModelInstances())
                    {
                        (void)root;
                        if (instance.state.accepted.layout.sourceAssetId == asset.sourceAssetId)
                            generations.push_back(instance.state.artifactGenerationKey);
                    }
                    std::sort(generations.begin(), generations.end());
                    generations.erase(std::unique(generations.begin(), generations.end()), generations.end());
                    if (!state.repairTargetsBuilt || state.repairGenerations != generations)
                    {
                        state.repairTargetsBuilt = true;
                        state.repairGenerations = std::move(generations);
                        targets.clear();
                        assets::AssetMetadata metadata;
                        assets::ModelImportSettings settings;
                        if (assets::LoadAssetMetadata(assets::GetAssetMetadataPath(project.ResolveAssetReference(reference)), metadata) == assets::AssetMetadataStatus::Success &&
                            assets::ReadModelImportSettings(metadata, settings) == assets::ModelImportSettingsStatus::Success)
                        {
                            std::unordered_set<std::uint64_t> listed, retired;
                            for (const auto &object : settings.objects)
                                if (object.retired && (object.sourceKey.starts_with("node/path/v1/") || object.sourceKey.starts_with("node/source/v1/")))
                                    retired.insert(object.localId);
                            for (const auto &[root, instance] : scene->GetStaticModelInstances())
                            {
                                (void)root;
                                const auto &layout = instance.state.accepted.layout;
                                if (layout.sourceAssetId != asset.sourceAssetId) continue;
                                for (const auto &old : layout.nodes)
                                {
                                    if (!listed.insert(old.sourceNodeId).second) continue;
                                    if (!retired.contains(old.sourceNodeId)) continue;
                                    std::string path = old.name;
                                    unsigned depth = 0;
                                    for (int parent = old.parentIndex; parent >= 0; parent = layout.nodes[parent].parentIndex)
                                    {
                                        if (++depth > 64 || path.size() > 512) { path = ".../" + path; break; }
                                        path = layout.nodes[parent].name + "/" + path;
                                    }
                                    targets.emplace_back(old.sourceNodeId, path + " [" + std::to_string(old.sourceNodeId) + "]");
                                }
                            }
                        }
                    }
                    ImGui::TextWrapped("Map this incoming node to a retired identity from a retained instance. Unchanged descendants inherit the repaired correspondence. Displaced IDs remain retired.");
                    const auto selected = std::find_if(targets.begin(), targets.end(), [&](const auto &target) { return target.first == state.repairTarget; });
                    ImGui::BeginDisabled(importRunning);
                    if (ImGui::BeginCombo("Retired source node", selected == targets.end() ? "Select retained node" : selected->second.c_str()))
                    {
                        for (const auto &[id, label] : targets)
                            if (ImGui::Selectable(label.c_str(), state.repairTarget == id))
                            { state.repairTarget = id; state.repair.reset(); state.repairMessage.clear(); }
                        ImGui::EndCombo();
                    }
                    if (targets.empty()) ImGui::TextDisabled("No retired nodes are available in retained instances of this source.");
                    ImGui::BeginDisabled(selected == targets.end());
                    if (ImGui::Button("Review Repair"))
                    {
                        assetimport::ModelNodeRepairProposal proposal;
                        if (assetimport::ModelNodeRepairService{}.Prepare(project, reference, identity.localId,
                            state.repairTarget, proposal, &state.repairMessage)) state.repair = std::move(proposal);
                        else state.repair.reset();
                    }
                    ImGui::EndDisabled();
                    if (state.repair)
                    {
                        ImGui::Text("Reviewed identity changes: %zu", state.repair->changedNodeCount);
                        ImGui::TextWrapped("Apply saves source import settings and reimports the model. Existing instance overrides are reconciled; conflicts retain their accepted instance.");
                        if (ImGui::Button("Apply Repair and Reimport"))
                        {
                            if (applyRepair(*state.repair, &state.repairMessage))
                            { state.repairMessage = "Repair saved. Reimport queued."; state.repair.reset(); }
                        }
                    }
                    ImGui::EndDisabled();
                    if (!state.repairMessage.empty()) ImGui::TextWrapped("%s", state.repairMessage.c_str());
                    ImGui::TreePop();
                }
            }
            Matrix("Exact Local Matrix", node.localTransform);
            Matrix("Source World Matrix", node.worldTransform);
            ImGui::Text("Mesh bindings: %zu", view.GetBindings()[index].size());
            for (const int bindingIndex : view.GetBindings()[index])
            {
                ImGui::PushID(bindingIndex);
                const auto &binding = asset.hierarchy.bindings[bindingIndex];
                ImGui::Text("Submesh %u | %s | animation node %d", binding.submeshIndex,
                    binding.skinned ? "Skinned" : "Rigid", binding.transformNodeIndex);
                Matrix("Baked Geometry Matrix", binding.bakedTransform);
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
}
