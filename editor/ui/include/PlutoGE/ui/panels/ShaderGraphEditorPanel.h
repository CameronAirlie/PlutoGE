#pragma once

#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/UnsavedSwitchGuard.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <GraphEditor.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui
{
    class ShaderGraphEditorPanel : public Panel, public IEditorDocument
    {
    public:
        ShaderGraphEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~ShaderGraphEditorPanel() override = default;

        void Render() override;

        // IEditorDocument
        [[nodiscard]] std::string GetDocumentName() const override;
        [[nodiscard]] bool HasUnsavedChanges() const override;
        bool SaveDocument(std::string &error) override;
        void DiscardChanges() override;
        void FocusDocument() override { Focus(); }

    private:
        void LoadGraph(const std::string &reference);

        std::string m_loadedReference;
        render::ShaderGraph m_graph;
        std::vector<render::ShaderGraph> m_undo, m_redo;
        bool m_editGesture = false;
        int m_selectedNodeId = 0;
        std::vector<int> m_selectedNodeIds;
        GraphEditor::Options m_graphOptions;
        GraphEditor::ViewState m_graphViewState;
        GraphEditor::FitOnScreen m_graphFit = GraphEditor::Fit_AllNodes;
        std::unordered_map<int, ImRect> m_nodeScreenRects;
        ImVec2 m_addNodePosition{60.0f, 60.0f};
        int m_resizingNodeId = 0;
        bool m_openAddNodePopup = false;
        bool m_showNodePreviews = true;
        bool m_dirty = false;
        UnsavedSwitchGuard m_switchGuard;
    };
}
