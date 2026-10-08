#include "PlutoGE/ui/ModelHierarchyView.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool value, const char *message)
    { if (!value) throw std::runtime_error(message); }
}

int main()
try
{
    using namespace PlutoGE;
    assets::ModelHierarchyAsset asset;
    asset.sourceAssetId = "hierarchy-inspector-source";
    Require(assets::SerializeAssetReference({asset.sourceAssetId, 42}, asset.meshReference), "Cannot encode mesh identity");
    asset.hierarchy.nodes = {{"Branch", 2}, {"Outside", -1}, {"Root", -1}, {"Needle", 0},
        {"Duplicate", 2}, {"Duplicate", 2}, {"", 1}};
    asset.hierarchy.nodes[2].localTransform = glm::translate(glm::mat4(1), glm::vec3(4, 0, 0));
    asset.hierarchy.nodes[0].localTransform[1][0] = .75f;
    asset.hierarchy.nodes[0].localTransform[3][1] = 3;
    asset.hierarchy.nodes[0].worldTransform = glm::mat4(0); // Never trust cached source worlds.
    asset.hierarchy.sceneRoots = {2};
    asset.hierarchy.bindings = {{0, 3, glm::mat4(1)}, {0, 7, glm::mat4(1)}, {1, 8, glm::mat4(1), 0, true}};
    assets::ModelImportSettings settings;
    std::string error;
    Require(assets::ReconcileModelNodeIdentities(asset.hierarchy, settings, asset.identities, &error), "Cannot identify test nodes");
    asset.identities[0].localId = (std::uint64_t{1} << 40) + 9;
    ui::ModelHierarchyView view;
    Require(view.Reset(asset, &error) && error.empty(), "Cannot build hierarchy presentation");
    Require(view.SelectedNodeCount() == 5 && view.SelectedBindingCount() == 2 && view.UnresolvedSelectedNodeCount() == 2,
        "Selected scene summary includes unselected nodes or loses unresolved identities");
    Require(view.GetStaticTransformError().empty() && view.GetBindings()[0] == std::vector<int>({0, 1}),
        "Unselected skinned binding prevented inspection or repeated bindings were lost");
    Require(view.GetAsset().identities[0].localId == asset.identities[0].localId &&
        view.GetAsset().hierarchy.nodes[0].localTransform == asset.hierarchy.nodes[0].localTransform &&
        view.GetAsset().hierarchy.nodes[0].worldTransform == asset.hierarchy.nodes[2].localTransform * asset.hierarchy.nodes[0].localTransform,
        "View truncated node IDs, lost exact local shear or used stale world transforms");
    Require(!view.IsSelectedNode(-1) && !view.IsSelectedNode(999) && !view.IsSelectedNode(1) && view.IsSelectedNode(3),
        "Selection mask is invalid");
    std::vector<unsigned char> expanded(7);
    auto rows = view.Rows(true, {}, expanded);
    Require(rows.size() == 1 && rows[0].nodeIndex == 2 && rows[0].hasChildren, "Collapsed tree is incorrect");
    expanded[2] = 1;
    rows = view.Rows(true, {}, expanded);
    Require(rows.size() == 4 && rows[1].nodeIndex == 0 && rows[1].depth == 1 && rows[1].hasChildren,
        "Expansion lost source ordering or forward-parent topology");
    rows = view.Rows(true, "nEeDlE", {});
    Require(rows.size() == 3 && rows[0].nodeIndex == 2 && rows[1].nodeIndex == 0 && rows[2].nodeIndex == 3 &&
        rows[2].depth == 2 && !rows[2].hasChildren, "Search did not include ancestors and expand matching branch");
    Require(view.Rows(true, "Outside", expanded).empty(), "Selected scene search included unselected source nodes");
    rows = view.Rows(false, "Outside", {});
    Require(rows.size() == 1 && rows[0].nodeIndex == 1, "All-source search omitted unselected root");
    Require(view.Rows(false, "No match", expanded).empty(), "Search returned unrelated nodes");
    auto invalid = asset;
    invalid.hierarchy.sceneRoots.push_back(0);
    Require(!view.Reset(invalid, &error) && !error.empty() && view.SelectedNodeCount() == 5, "Overlapping roots replaced valid view");
    invalid = asset;
    invalid.hierarchy.nodes[2].parentNodeIndex = 3;
    Require(!view.Reset(invalid, &error) && view.SelectedNodeCount() == 5, "Cyclic topology replaced valid view");
    auto animated = asset;
    animated.hierarchy.bindings[0].skinned = true;
    Require(view.Reset(animated, &error) && !view.GetStaticTransformError().empty(),
        "Animated source should remain inspectable with a static-preparation diagnostic");
    auto subtree = asset;
    subtree.hierarchy.sceneRoots = {0};
    Require(view.Reset(subtree, &error), "Cannot view selected subtree");
    rows = view.Rows(true, "Needle", {});
    Require(rows.size() == 2 && rows[0].nodeIndex == 0 && rows[0].depth == 0 && rows[1].depth == 1,
        "Selected subtree pulled an unselected ancestor into the tree");
    subtree.hierarchy.sceneRoots.clear();
    Require(view.Reset(subtree, &error) && view.Rows(true, {}, expanded).empty() && view.SelectedNodeCount() == 0,
        "Empty scene implicitly selected all source nodes");
    // A large reverse-ordered chain exercises nonrecursive indexing and the
    // linear ancestor search pass rather than one parent walk per matching node.
    assets::ModelHierarchyAsset deep;
    deep.sourceAssetId = asset.sourceAssetId;
    constexpr int count = 10000;
    deep.hierarchy.nodes.resize(count);
    deep.identities.resize(count);
    for (int index = 0; index < count; ++index)
    {
        deep.hierarchy.nodes[index].name = "Match";
        deep.hierarchy.nodes[index].parentNodeIndex = index + 1 < count ? index + 1 : -1;
        deep.identities[index] = {static_cast<std::uint64_t>(index + 100), "node/" + std::to_string(index), assets::ModelNodeIdentityStatus::Matched};
    }
    deep.hierarchy.sceneRoots = {count - 1};
    Require(view.Reset(std::move(deep), &error), "Deep hierarchy indexing failed");
    rows = view.Rows(true, "Match", {});
    Require(rows.size() == count && rows.front().depth == 0 && rows.back().depth == count - 1 && rows.back().nodeIndex == 0,
        "Deep hierarchy traversal or search failed");
    std::cout << "Model hierarchy presentation checks passed.\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
