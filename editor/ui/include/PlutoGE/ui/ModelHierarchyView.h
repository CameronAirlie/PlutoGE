#pragma once

#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include <string_view>

namespace PlutoGE::ui
{
    struct ModelHierarchyRow
    {
        int nodeIndex = -1;
        unsigned depth = 0;
        bool hasChildren = false;
    };

    // Read-only presentation of one verified snapshot. All traversal is iterative;
    // source indices are UI addresses only and never persistent node identities.
    class ModelHierarchyView
    {
    public:
        bool Reset(assets::ModelHierarchyAsset asset, std::string *error = nullptr);
        const assets::ModelHierarchyAsset &GetAsset() const { return m_asset; }
        const std::vector<std::vector<int>> &GetChildren() const { return m_children; }
        const std::vector<std::vector<int>> &GetBindings() const { return m_bindings; }
        bool IsSelectedNode(int index) const;
        std::size_t SelectedNodeCount() const { return m_selectedNodeCount; }
        std::size_t UnresolvedSelectedNodeCount() const { return m_unresolvedSelectedNodeCount; }
        std::size_t SelectedBindingCount() const { return m_selectedBindingCount; }
        const std::string &GetStaticTransformError() const { return m_staticTransformError; }
        // Search includes ancestors and temporarily expands matching branches.
        // An empty search respects expansion; missing flags mean collapsed.
        std::vector<ModelHierarchyRow> Rows(bool selectedOnly, std::string_view search,
            const std::vector<unsigned char> &expanded) const;
    private:
        assets::ModelHierarchyAsset m_asset;
        std::vector<std::vector<int>> m_children, m_bindings;
        std::vector<unsigned char> m_selected;
        std::vector<int> m_roots, m_preorder;
        std::size_t m_selectedNodeCount = 0, m_unresolvedSelectedNodeCount = 0, m_selectedBindingCount = 0;
        std::string m_staticTransformError;
    };
}
