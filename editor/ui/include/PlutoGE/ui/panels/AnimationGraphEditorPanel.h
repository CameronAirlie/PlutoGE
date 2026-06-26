#pragma once

#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <GraphEditor.h>

#include <string>
#include <vector>

namespace PlutoGE::ui
{
    class AnimationGraphEditorPanel : public Panel
    {
    public:
        AnimationGraphEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~AnimationGraphEditorPanel() override = default;

        void Render() override;

    private:
        void LoadActiveGraph();

        std::string m_loadedReference;
        assets::AnimationGraphAsset m_graph;
        int m_selectedStateId = 0;
        std::vector<int> m_selectedStateIds;
        int m_selectedTransitionId = 0;
        GraphEditor::Options m_graphOptions;
        GraphEditor::ViewState m_graphViewState;
        GraphEditor::FitOnScreen m_graphFit = GraphEditor::Fit_AllNodes;
        ImVec2 m_addStatePosition{80.0f, 80.0f};
        bool m_openAddStatePopup = false;
        bool m_dirty = false;
    };
}
