#pragma once

#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <GraphEditor.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui
{
    class ShaderGraphEditorPanel : public Panel
    {
    public:
        ShaderGraphEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~ShaderGraphEditorPanel() override = default;

        void Render() override;

    private:
        void LoadActiveGraph();

        std::string m_loadedReference;
        render::ShaderGraph m_graph;
        int m_selectedNodeId = 0;
        std::vector<int> m_selectedNodeIds;
        GraphEditor::Options m_graphOptions;
        GraphEditor::ViewState m_graphViewState;
        GraphEditor::FitOnScreen m_graphFit = GraphEditor::Fit_AllNodes;
        std::unordered_map<int, ImRect> m_nodeScreenRects;
        int m_resizingNodeId = 0;
        bool m_dirty = false;
    };
}
