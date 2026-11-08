#pragma once

#include "PlutoGE/ui/animgraph/AnimationGraphDocument.h"
#include "PlutoGE/ui/animgraph/AnimationGraphLiveSession.h"
#include "PlutoGE/ui/animgraph/StateMachineCanvas.h"
#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <string>

namespace PlutoGE::ui
{
    // Hierarchical state machine editor: an outline/parameters/layers sidebar,
    // a canvas showing one group at a time, and a selection inspector.
    class AnimationGraphEditorPanel : public Panel, public IEditorDocument
    {
    public:
        AnimationGraphEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~AnimationGraphEditorPanel() override = default;

        void Render() override;

        // While focused, the panel owns undo/redo/save shortcuts.
        bool OwnsKeyboardFocus() const;
        // IEditorDocument
        [[nodiscard]] std::string GetDocumentName() const override;
        [[nodiscard]] bool HasUnsavedChanges() const override { return m_document.IsDirty(); }
        bool SaveDocument(std::string &error) override;
        void DiscardChanges() override { LoadActiveGraph(); }
        void FocusDocument() override { Focus(); }

    private:
        void LoadActiveGraph();
        bool Save();
        void HandleShortcuts();
        void DrawToolbar();
        void DrawStatusBar();

        std::string m_loadedReference;
        animgraph::AnimationGraphDocument m_document;
        animgraph::StateMachineCanvas m_canvas;
        animgraph::AnimationGraphLiveSession m_live;
        std::string m_canvasSearch;
        bool m_liveEnabled = true;
        bool m_focused = false;
    };
}
