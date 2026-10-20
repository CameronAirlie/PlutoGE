#pragma once

#include "PlutoGE/ui/animgraph/AnimationGraphDocument.h"
#include "PlutoGE/ui/animgraph/StateMachineView.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui::animgraph
{
    // Live playback of an entity using the graph, expressed in graph state ids.
    struct LivePlaybackOverlay
    {
        int activeStateId = 0;
        float activeStateProgress = 0.0f;
        int transitionFromStateId = 0;
        int transitionToStateId = 0;
        float transitionProgress = 0.0f;
    };

    struct CanvasOverlay
    {
        std::optional<LivePlaybackOverlay> live;
        // Nodes whose title or contents match are emphasised; others fade.
        std::string search;
    };

    // Draws one level of the state machine and turns mouse/keyboard input into
    // document commands. Camera state is remembered per group.
    class StateMachineCanvas
    {
    public:
        void Draw(AnimationGraphDocument &document, const StateMachineView &view, const CanvasOverlay &overlay);

        void RequestFrameAll() { m_frameRequest = FrameRequest::All; }
        void RequestFrameSelection() { m_frameRequest = FrameRequest::Selection; }
        // Starts drawing a transition from the node; the next click picks the target.
        void BeginConnect(NodeRef source);
        // True while a drag is in progress; edits commit when it ends.
        bool IsInteracting() const { return m_drag != DragMode::None; }
        float Zoom() const;

    private:
        enum class DragMode
        {
            None,
            Pending,      // Left button down on a node, not yet moved.
            MoveNodes,
            Pan,
            BoxSelect,
            ConnectDrag,  // Dragging from a node's connection handle.
        };

        enum class FrameRequest
        {
            None,
            All,
            Selection,
        };

        struct Camera
        {
            ImVec2 offset{0.0f, 0.0f};
            float zoom = 1.0f;
            bool framed = false;
        };

        struct EdgeGeometry
        {
            ImVec2 start;
            ImVec2 end;
            ImVec2 direction;
            ImVec2 normal;
        };

        struct Frame; // Per-draw state, defined in the implementation.

        void HandleInput(Frame &frame);
        void HandleKeyboard(Frame &frame);
        void DrawGrid(Frame &frame) const;
        void DrawEdges(Frame &frame) const;
        void DrawNodes(Frame &frame) const;
        void DrawConnectPreview(Frame &frame) const;
        void DrawBoxSelection(Frame &frame) const;
        void DrawContextMenu(Frame &frame);
        void DrawHoverTooltip(Frame &frame) const;
        void ApplyFrameRequest(Frame &frame);

        Camera &CurrentCamera(int groupId) { return m_cameras[groupId]; }

        std::unordered_map<int, Camera> m_cameras;
        int m_viewGroupId = assets::kAnimationGraphRootGroupId;
        FrameRequest m_frameRequest = FrameRequest::None;

        DragMode m_drag = DragMode::None;
        int m_pressedNode = -1;
        bool m_pressedNodeWasSelected = false;
        ImVec2 m_dragStart{0.0f, 0.0f};
        // Pending click-to-connect started from the context menu or inspector.
        std::optional<NodeRef> m_connectSource;
        bool m_connectFromMenu = false;

        // Context menu target: node index, edge index, or the empty canvas.
        int m_contextNode = -1;
        int m_contextEdge = -1;
        ImVec2 m_contextCanvasPosition{0.0f, 0.0f};
        bool m_openContextMenu = false;
    };
}
