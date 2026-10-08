#include "PlutoGE/ui/ModelHierarchyView.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace PlutoGE::ui
{
    bool ModelHierarchyView::Reset(assets::ModelHierarchyAsset asset, std::string *error)
    {
        try
        {
            // Reuse the artifact's bounded ownership/identity/provenance checks.
            std::string bytes;
            if (!assets::SerializeModelHierarchyAsset(asset, bytes, error)) return false;
            assetimport::ImportedModelHierarchy rebuilt;
            if (!assetimport::BuildImportedModelHierarchy(asset.hierarchy.nodes, rebuilt, error)) return false;
            asset.hierarchy.nodes = std::move(rebuilt.nodes);
            ModelHierarchyView candidate;
            candidate.m_roots = std::move(rebuilt.sceneRoots);
            candidate.m_asset = std::move(asset);
            const auto &hierarchy = candidate.m_asset.hierarchy;
            candidate.m_children.resize(hierarchy.nodes.size());
            candidate.m_bindings.resize(hierarchy.nodes.size());
            candidate.m_selected.resize(hierarchy.nodes.size());
            for (int i = 0; i < static_cast<int>(hierarchy.nodes.size()); ++i)
                if (hierarchy.nodes[i].parentNodeIndex >= 0)
                    candidate.m_children[hierarchy.nodes[i].parentNodeIndex].push_back(i);
            std::vector<int> pending;
            for (const int root : hierarchy.sceneRoots)
            {
                pending.push_back(root);
                while (!pending.empty())
                {
                    const int index = pending.back();
                    pending.pop_back();
                    if (candidate.m_selected[index]) throw std::runtime_error("Selected hierarchy roots overlap.");
                    candidate.m_selected[index] = 1;
                    ++candidate.m_selectedNodeCount;
                    if (!candidate.m_asset.identities[index].localId) ++candidate.m_unresolvedSelectedNodeCount;
                    const auto &children = candidate.m_children[index];
                    pending.insert(pending.end(), children.begin(), children.end());
                }
            }
            for (int i = 0; i < static_cast<int>(hierarchy.bindings.size()); ++i)
            {
                const int node = hierarchy.bindings[i].nodeIndex;
                candidate.m_bindings[node].push_back(i);
                if (candidate.m_selected[node]) ++candidate.m_selectedBindingCount;
            }
            pending.assign(candidate.m_roots.rbegin(), candidate.m_roots.rend());
            while (!pending.empty())
            {
                const int index = pending.back();
                pending.pop_back();
                candidate.m_preorder.push_back(index);
                const auto &children = candidate.m_children[index];
                pending.insert(pending.end(), children.rbegin(), children.rend());
            }
            std::vector<assetimport::StaticModelBindingTransform> transforms;
            assetimport::PrepareStaticModelBindingTransforms(hierarchy, transforms, &candidate.m_staticTransformError);
            *this = std::move(candidate);
            if (error) error->clear();
            return true;
        }
        catch (const std::exception &exception)
        {
            if (error) *error = exception.what();
            return false;
        }
    }

    bool ModelHierarchyView::IsSelectedNode(int index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < m_selected.size() && m_selected[index];
    }

    std::vector<ModelHierarchyRow> ModelHierarchyView::Rows(bool selectedOnly, std::string_view search,
        const std::vector<unsigned char> &expanded) const
    {
        std::vector<unsigned char> visible(m_children.size());
        const auto equal = [](char left, char right)
        { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); };
        for (int index = 0; index < static_cast<int>(visible.size()); ++index)
        {
            const auto &name = m_asset.hierarchy.nodes[index].name;
            visible[index] = (!selectedOnly || m_selected[index]) &&
                (search.empty() || std::search(name.begin(), name.end(), search.begin(), search.end(), equal) != name.end());
        }
        // One reverse pass propagates matches to eligible ancestors, avoiding
        // quadratic work when a deep source tree has many matching descendants.
        if (!search.empty())
            for (auto node = m_preorder.rbegin(); node != m_preorder.rend(); ++node)
            {
                const int parent = m_asset.hierarchy.nodes[*node].parentNodeIndex;
                if (visible[*node] && parent >= 0 && (!selectedOnly || m_selected[parent])) visible[parent] = 1;
            }
        std::vector<ModelHierarchyRow> rows;
        std::vector<std::pair<int, unsigned>> pending;
        const auto &roots = selectedOnly ? m_asset.hierarchy.sceneRoots : m_roots;
        for (auto root = roots.rbegin(); root != roots.rend(); ++root) pending.emplace_back(*root, 0);
        while (!pending.empty())
        {
            const auto [index, depth] = pending.back();
            pending.pop_back();
            if (!visible[index]) continue;
            const auto &children = m_children[index];
            const bool hasChildren = std::any_of(children.begin(), children.end(), [&](int child) { return visible[child] != 0; });
            rows.push_back({index, depth, hasChildren});
            if (!search.empty() || (static_cast<std::size_t>(index) < expanded.size() && expanded[index]))
                for (auto child = children.rbegin(); child != children.rend(); ++child) pending.emplace_back(*child, depth + 1);
        }
        return rows;
    }
}
