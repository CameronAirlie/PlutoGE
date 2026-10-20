#include "PlutoGE/ui/animgraph/StateMachineCanvas.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>

namespace PlutoGE::ui::animgraph
{
    namespace ag = assets::animgraph;

    namespace
    {
        constexpr float kMinZoom = 0.2f;
        constexpr float kMaxZoom = 2.0f;
        constexpr float kDragThreshold = 4.0f;
        constexpr float kHandleRadius = 5.0f;
        constexpr float kEdgeHitDistance = 6.0f;
        constexpr float kParallelEdgeOffset = 6.0f;

        // Kind colours are semantic, so they stay fixed across editor themes.
        constexpr ImU32 kClipStateColor = IM_COL32(86, 156, 214, 255);
        constexpr ImU32 kBlendStateColor = IM_COL32(170, 130, 230, 255);
        constexpr ImU32 kGroupColor = IM_COL32(72, 184, 160, 255);
        constexpr ImU32 kAnyStateColor = IM_COL32(232, 170, 64, 255);

        ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
        ImVec2 Sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
        ImVec2 Mul(ImVec2 a, float s) { return ImVec2(a.x * s, a.y * s); }
        float Length(ImVec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
        ImVec2 Lerp(ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
        ImVec2 ToImVec2(Vec2 v) { return ImVec2(v.x, v.y); }

        ImU32 WithAlpha(ImU32 color, float alpha)
        {
            const auto a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF));
            return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
        }

        float DistanceToSegment(ImVec2 point, ImVec2 a, ImVec2 b)
        {
            const ImVec2 ab = Sub(b, a);
            const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
            const float t = lengthSquared > 0.0f
                                ? std::clamp(((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / lengthSquared, 0.0f, 1.0f)
                                : 0.0f;
            return Length(Sub(point, Lerp(a, b, t)));
        }

        // Where a ray from the centre of a box leaves the box.
        ImVec2 BorderPoint(ImVec2 center, ImVec2 halfSize, ImVec2 direction)
        {
            const float tx = std::abs(direction.x) > 1e-5f ? halfSize.x / std::abs(direction.x) : FLT_MAX;
            const float ty = std::abs(direction.y) > 1e-5f ? halfSize.y / std::abs(direction.y) : FLT_MAX;
            return Add(center, Mul(direction, std::min(tx, ty)));
        }

        bool ContainsInsensitive(const std::string &text, const std::string &needle)
        {
            const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b)
                                        { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            return it != text.end();
        }

        void DrawClippedText(ImDrawList *drawList, float fontSize, ImVec2 position, float maxWidth, ImU32 color, const std::string &text)
        {
            if (fontSize < 5.0f || maxWidth <= 0.0f)
                return;
            ImFont *font = ImGui::GetFont();
            if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
            {
                drawList->AddText(font, fontSize, position, color, text.c_str());
                return;
            }
            const float ellipsisWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, "...").x;
            std::size_t length = text.size();
            while (length > 0 && font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str(), text.c_str() + length).x + ellipsisWidth > maxWidth)
                --length;
            // Never split a UTF-8 sequence.
            while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80)
                --length;
            const std::string clipped = text.substr(0, length) + "...";
            drawList->AddText(font, fontSize, position, color, clipped.c_str());
        }

        ImU32 KindColor(const ViewNode &node)
        {
            switch (node.kind)
            {
            case ViewNodeKind::Group:
                return kGroupColor;
            case ViewNodeKind::AnyState:
                return kAnyStateColor;
            case ViewNodeKind::Parent:
                return GetEditorColorU32(EditorColor::TextMuted);
            case ViewNodeKind::State:
            default:
                return node.isBlendSpace ? kBlendStateColor : kClipStateColor;
            }
        }

        bool IsProxy(const ViewNode &node)
        {
            return node.kind == ViewNodeKind::AnyState || node.kind == ViewNodeKind::Parent;
        }

        // The document selection entry for a canvas node; Parent is not selectable.
        std::optional<NodeRef> SelectionRef(const ViewNode &node)
        {
            if (node.kind == ViewNodeKind::Parent)
                return std::nullopt;
            return node.ref;
        }

        // Transition endpoint a drag from this node creates.
        std::optional<NodeRef> ConnectionSource(const ViewNode &node)
        {
            return node.kind == ViewNodeKind::Parent ? std::nullopt : std::optional<NodeRef>(node.ref);
        }

        std::optional<NodeRef> ConnectionTarget(const ViewNode &node)
        {
            return node.kind == ViewNodeKind::State || node.kind == ViewNodeKind::Group ? std::optional<NodeRef>(node.ref)
                                                                                         : std::nullopt;
        }
    }

    struct StateMachineCanvas::Frame
    {
        AnimationGraphDocument &document;
        const StateMachineView &view;
        const CanvasOverlay &overlay;
        Camera &camera;
        ImDrawList *drawList = nullptr;
        ImVec2 origin;
        ImVec2 size;
        bool hovered = false;

        int hoveredNode = -1;
        int hoveredEdge = -1;
        bool hoveredHandle = false;

        std::vector<EdgeGeometry> edges;
        std::vector<bool> selected;
        std::vector<bool> dimmed;
        std::vector<int> worstIssue; // -1 none, otherwise DiagnosticSeverity.
        int liveNode = -1;
        int liveEdge = -1;

        ImVec2 ToScreen(ImVec2 canvas) const
        {
            return ImVec2(origin.x + camera.offset.x + canvas.x * camera.zoom, origin.y + camera.offset.y + canvas.y * camera.zoom);
        }
        ImVec2 ToCanvas(ImVec2 screen) const
        {
            return ImVec2((screen.x - origin.x - camera.offset.x) / camera.zoom, (screen.y - origin.y - camera.offset.y) / camera.zoom);
        }
        ImVec2 NodeMin(std::size_t index) const { return ToScreen(ToImVec2(view.nodes[index].position)); }
        ImVec2 NodeMax(std::size_t index) const
        {
            const auto &node = view.nodes[index];
            return ToScreen(ImVec2(node.position.x + node.size.x, node.position.y + node.size.y));
        }
        ImVec2 NodeCenter(std::size_t index) const { return Lerp(NodeMin(index), NodeMax(index), 0.5f); }
        ImVec2 HandleCenter(std::size_t index) const
        {
            return ImVec2(NodeMax(index).x, (NodeMin(index).y + NodeMax(index).y) * 0.5f);
        }
        float Scale(float value) const { return value * camera.zoom; }
    };

    float StateMachineCanvas::Zoom() const
    {
        const auto it = m_cameras.find(m_viewGroupId);
        return it == m_cameras.end() ? 1.0f : it->second.zoom;
    }

    void StateMachineCanvas::BeginConnect(NodeRef source)
    {
        m_connectSource = source;
        m_connectFromMenu = true;
        m_drag = DragMode::None;
    }

    void StateMachineCanvas::Draw(AnimationGraphDocument &document, const StateMachineView &view, const CanvasOverlay &overlay)
    {
        if (view.groupId != m_viewGroupId)
        {
            m_viewGroupId = view.groupId;
            m_drag = DragMode::None;
            m_connectSource.reset();
        }

        Frame frame{document, view, overlay, CurrentCamera(view.groupId)};
        frame.origin = ImGui::GetCursorScreenPos();
        frame.size = ImGui::GetContentRegionAvail();
        frame.size.x = std::max(frame.size.x, 50.0f);
        frame.size.y = std::max(frame.size.y, 50.0f);
        frame.drawList = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton("##StateMachineCanvas", frame.size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        frame.hovered = ImGui::IsItemHovered();

        if (!frame.camera.framed)
        {
            m_frameRequest = FrameRequest::All;
            frame.camera.framed = true;
        }
        ApplyFrameRequest(frame);

        // Derived per-node and per-edge presentation state.
        const auto &selection = document.Selection();
        const auto &graph = document.Graph();
        frame.selected.resize(view.nodes.size());
        frame.dimmed.resize(view.nodes.size());
        frame.worstIssue.assign(view.nodes.size(), -1);
        for (std::size_t index = 0; index < view.nodes.size(); ++index)
        {
            const auto &node = view.nodes[index];
            const auto ref = SelectionRef(node);
            frame.selected[index] = ref && selection.Contains(*ref);
            if (!overlay.search.empty() && node.kind != ViewNodeKind::Parent)
            {
                bool match = ContainsInsensitive(node.title, overlay.search) || ContainsInsensitive(node.subtitle, overlay.search);
                if (!match && node.kind == ViewNodeKind::Group)
                    for (const int stateId : ag::CollectStatesWithin(graph, node.ref.id))
                        if (const auto *state = ag::FindState(graph, stateId); state && ContainsInsensitive(state->name, overlay.search))
                        {
                            match = true;
                            break;
                        }
                frame.dimmed[index] = !match;
            }
        }
        for (const auto &diagnostic : document.Diagnostics())
        {
            if (!diagnostic.node || diagnostic.severity == assets::animgraph::DiagnosticSeverity::Info)
                continue;
            if (const int index = view.FindNode(*diagnostic.node); index >= 0)
                frame.worstIssue[static_cast<std::size_t>(index)] =
                    std::max(frame.worstIssue[static_cast<std::size_t>(index)], static_cast<int>(diagnostic.severity));
        }

        frame.edges.reserve(view.edges.size());
        for (const auto &edge : view.edges)
        {
            const ImVec2 a = frame.NodeCenter(edge.from);
            const ImVec2 b = frame.NodeCenter(edge.to);
            const float length = std::max(Length(Sub(b, a)), 0.0001f);
            const ImVec2 direction = Mul(Sub(b, a), 1.0f / length);
            const ImVec2 normal(-direction.y, direction.x);
            const ImVec2 offset = edge.hasReverse ? Mul(normal, frame.Scale(kParallelEdgeOffset)) : ImVec2(0.0f, 0.0f);
            const auto halfSize = [&](std::size_t index) { return Mul(ToImVec2(view.nodes[index].size), frame.camera.zoom * 0.5f); };
            frame.edges.push_back(EdgeGeometry{
                .start = BorderPoint(Add(a, offset), halfSize(edge.from), direction),
                .end = BorderPoint(Add(b, offset), halfSize(edge.to), Mul(direction, -1.0f)),
                .direction = direction,
                .normal = normal,
            });
        }

        if (overlay.live)
        {
            frame.liveNode = view.NodeShowingState(graph, overlay.live->activeStateId);
            if (overlay.live->transitionFromStateId != 0)
            {
                const int from = view.NodeShowingState(graph, overlay.live->transitionFromStateId);
                const int to = view.NodeShowingState(graph, overlay.live->transitionToStateId);
                for (std::size_t index = 0; index < view.edges.size(); ++index)
                    if (static_cast<int>(view.edges[index].from) == from && static_cast<int>(view.edges[index].to) == to)
                        frame.liveEdge = static_cast<int>(index);
            }
        }

        // Hover: connection handle, then the top-most node, then edges.
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (frame.hovered)
        {
            for (int index = static_cast<int>(view.nodes.size()) - 1; index >= 0; --index)
            {
                const auto i = static_cast<std::size_t>(index);
                const ImVec2 min = frame.NodeMin(i);
                const ImVec2 max = frame.NodeMax(i);
                const bool canSource = ConnectionSource(view.nodes[i]).has_value();
                if (canSource && Length(Sub(mouse, frame.HandleCenter(i))) <= std::max(8.0f, frame.Scale(kHandleRadius + 3.0f)))
                {
                    frame.hoveredNode = index;
                    frame.hoveredHandle = true;
                    break;
                }
                if (mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y)
                {
                    frame.hoveredNode = index;
                    break;
                }
            }
            if (frame.hoveredNode < 0)
            {
                float best = kEdgeHitDistance;
                for (std::size_t index = 0; index < frame.edges.size(); ++index)
                {
                    const float distance = DistanceToSegment(mouse, frame.edges[index].start, frame.edges[index].end);
                    if (distance <= best)
                    {
                        best = distance;
                        frame.hoveredEdge = static_cast<int>(index);
                    }
                }
            }
        }

        HandleInput(frame);
        HandleKeyboard(frame);

        frame.drawList->PushClipRect(frame.origin, Add(frame.origin, frame.size), true);
        DrawGrid(frame);
        DrawEdges(frame);
        DrawNodes(frame);
        DrawConnectPreview(frame);
        DrawBoxSelection(frame);
        if (view.nodes.size() <= 2)
        {
            const char *hint = "Right-click to add a state, or drag from a node's handle to connect.";
            const ImVec2 textSize = ImGui::CalcTextSize(hint);
            frame.drawList->AddText(ImVec2(frame.origin.x + (frame.size.x - textSize.x) * 0.5f, frame.origin.y + frame.size.y * 0.5f),
                                    GetEditorColorU32(EditorColor::TextMuted), hint);
        }
        frame.drawList->PopClipRect();

        DrawHoverTooltip(frame);
        DrawContextMenu(frame);
    }

    void StateMachineCanvas::ApplyFrameRequest(Frame &frame)
    {
        if (m_frameRequest == FrameRequest::None)
            return;

        float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
        const auto &selection = frame.document.Selection();
        for (const auto &node : frame.view.nodes)
        {
            const auto ref = SelectionRef(node);
            if (m_frameRequest == FrameRequest::Selection && !selection.nodes.empty() && !(ref && selection.Contains(*ref)))
                continue;
            minX = std::min(minX, node.position.x);
            minY = std::min(minY, node.position.y);
            maxX = std::max(maxX, node.position.x + node.size.x);
            maxY = std::max(maxY, node.position.y + node.size.y);
        }
        m_frameRequest = FrameRequest::None;
        if (minX > maxX)
            return;

        constexpr float margin = 80.0f;
        const float width = maxX - minX + margin * 2.0f;
        const float height = maxY - minY + margin * 2.0f;
        frame.camera.zoom = std::clamp(std::min(frame.size.x / width, frame.size.y / height), kMinZoom, 1.2f);
        const ImVec2 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
        frame.camera.offset = ImVec2(frame.size.x * 0.5f - center.x * frame.camera.zoom,
                                     frame.size.y * 0.5f - center.y * frame.camera.zoom);
    }

    void StateMachineCanvas::HandleInput(Frame &frame)
    {
        ImGuiIO &io = ImGui::GetIO();
        auto &document = frame.document;
        const auto &view = frame.view;
        const bool additive = io.KeyCtrl || io.KeyShift;

        // Zoom around the cursor.
        if (frame.hovered && io.MouseWheel != 0.0f)
        {
            const ImVec2 anchor = frame.ToCanvas(io.MousePos);
            frame.camera.zoom = std::clamp(frame.camera.zoom * std::pow(1.15f, io.MouseWheel), kMinZoom, kMaxZoom);
            frame.camera.offset = ImVec2(io.MousePos.x - frame.origin.x - anchor.x * frame.camera.zoom,
                                         io.MousePos.y - frame.origin.y - anchor.y * frame.camera.zoom);
        }

        // Pan with the middle button, or the right button once it drags.
        if (frame.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            m_drag = DragMode::Pan;
        if (m_drag == DragMode::None && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, kDragThreshold))
            m_drag = DragMode::Pan;
        if (m_drag == DragMode::Pan)
        {
            frame.camera.offset = Add(frame.camera.offset, io.MouseDelta);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
                m_drag = DragMode::None;
            return;
        }

        const auto hoveredTarget = frame.hoveredNode >= 0 ? ConnectionTarget(view.nodes[static_cast<std::size_t>(frame.hoveredNode)])
                                                          : std::nullopt;

        if (frame.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (m_connectSource)
            {
                m_connectSource.reset();
                return;
            }
            m_contextNode = frame.hoveredNode;
            m_contextEdge = frame.hoveredEdge;
            m_contextCanvasPosition = frame.ToCanvas(io.MousePos);
            m_openContextMenu = true;
            if (m_contextNode >= 0)
            {
                const auto ref = SelectionRef(view.nodes[static_cast<std::size_t>(m_contextNode)]);
                if (ref && !document.Selection().Contains(*ref))
                    document.SelectNode(*ref, false);
            }
            else if (m_contextEdge >= 0)
            {
                const auto &edge = view.edges[static_cast<std::size_t>(m_contextEdge)];
                document.SelectTransitions(edge.transitionIds, edge.transitionIds.front());
            }
            return;
        }

        if (frame.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && frame.hoveredNode >= 0 && !frame.hoveredHandle)
        {
            const auto &node = view.nodes[static_cast<std::size_t>(frame.hoveredNode)];
            if (node.kind == ViewNodeKind::Group)
                document.EnterGroup(node.ref.id);
            else if (node.kind == ViewNodeKind::Parent)
                document.NavigateUp();
            m_drag = DragMode::None;
            return;
        }

        if (frame.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_dragStart = io.MousePos;
            if (m_connectSource && m_connectFromMenu)
            {
                if (hoveredTarget)
                    document.AddTransition(*m_connectSource, *hoveredTarget);
                m_connectSource.reset();
                return;
            }
            if (frame.hoveredHandle)
            {
                m_connectSource = ConnectionSource(view.nodes[static_cast<std::size_t>(frame.hoveredNode)]);
                m_connectFromMenu = false;
                m_drag = DragMode::ConnectDrag;
            }
            else if (frame.hoveredNode >= 0)
            {
                const auto &node = view.nodes[static_cast<std::size_t>(frame.hoveredNode)];
                m_pressedNode = frame.hoveredNode;
                m_pressedNodeWasSelected = frame.selected[static_cast<std::size_t>(frame.hoveredNode)];
                if (const auto ref = SelectionRef(node); ref && !m_pressedNodeWasSelected)
                    document.SelectNode(*ref, additive);
                m_drag = DragMode::Pending;
            }
            else if (frame.hoveredEdge >= 0)
            {
                const auto &edge = view.edges[static_cast<std::size_t>(frame.hoveredEdge)];
                document.SelectTransitions(edge.transitionIds, edge.transitionIds.front());
            }
            else
            {
                if (!additive)
                    document.ClearSelection();
                m_drag = DragMode::BoxSelect;
            }
        }

        if (m_drag == DragMode::Pending && ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragThreshold))
            m_drag = DragMode::MoveNodes;

        if (m_drag == DragMode::MoveNodes && m_pressedNode >= 0)
        {
            const ImVec2 delta = Mul(io.MouseDelta, 1.0f / frame.camera.zoom);
            const auto &pressed = view.nodes[static_cast<std::size_t>(m_pressedNode)];
            if (IsProxy(pressed))
            {
                auto layout = ResolveProxyLayout(document.Graph(), view.groupId);
                layout.placed = true;
                if (pressed.kind == ViewNodeKind::AnyState)
                {
                    layout.anyStateX += delta.x;
                    layout.anyStateY += delta.y;
                }
                else
                {
                    layout.parentX += delta.x;
                    layout.parentY += delta.y;
                }
                document.SetGroupLayout(view.groupId, layout);
            }
            else
                document.MoveNodes(document.SelectedContent(), delta.x, delta.y);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (m_drag == DragMode::Pending && m_pressedNode >= 0 && m_pressedNodeWasSelected)
            {
                // A plain click on a selected node narrows the selection to it.
                if (const auto ref = SelectionRef(view.nodes[static_cast<std::size_t>(m_pressedNode)]))
                    document.SelectNode(*ref, additive);
            }
            else if (m_drag == DragMode::BoxSelect)
            {
                const ImVec2 a = m_dragStart;
                const ImVec2 b = io.MousePos;
                const ImVec2 min(std::min(a.x, b.x), std::min(a.y, b.y));
                const ImVec2 max(std::max(a.x, b.x), std::max(a.y, b.y));
                if (max.x - min.x > kDragThreshold || max.y - min.y > kDragThreshold)
                {
                    for (std::size_t index = 0; index < view.nodes.size(); ++index)
                    {
                        const ImVec2 nodeMin = frame.NodeMin(index);
                        const ImVec2 nodeMax = frame.NodeMax(index);
                        const auto ref = SelectionRef(view.nodes[index]);
                        if (ref && nodeMax.x >= min.x && nodeMin.x <= max.x && nodeMax.y >= min.y && nodeMin.y <= max.y &&
                            !document.Selection().Contains(*ref))
                            document.SelectNode(*ref, true);
                    }
                }
            }
            else if (m_drag == DragMode::ConnectDrag && m_connectSource)
            {
                if (hoveredTarget)
                    document.AddTransition(*m_connectSource, *hoveredTarget);
                else if (frame.hovered && frame.hoveredNode < 0)
                {
                    // Dropping on empty canvas creates the destination too.
                    const auto source = *m_connectSource;
                    const ImVec2 at = frame.ToCanvas(io.MousePos);
                    const int stateId = document.AddState(at.x, at.y - kStateNodeSize.y * 0.5f);
                    document.AddTransition(source, NodeRef::ForState(stateId));
                }
                m_connectSource.reset();
            }
            if (m_drag != DragMode::Pan)
                m_drag = DragMode::None;
            m_pressedNode = -1;
        }
    }

    void StateMachineCanvas::HandleKeyboard(Frame &frame)
    {
        const ImGuiIO &io = ImGui::GetIO();
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (io.WantTextInput || !(frame.hovered || focused) ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            return;

        auto &document = frame.document;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (m_connectSource)
                m_connectSource.reset();
            else
                document.ClearSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            document.DeleteSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !io.KeyCtrl)
            document.NavigateUp();
        if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl)
            RequestFrameSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false))
            document.GroupSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            document.DuplicateSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            document.ClearSelection();
            for (const auto &node : frame.view.nodes)
                if (node.kind == ViewNodeKind::State || node.kind == ViewNodeKind::Group)
                    document.SelectNode(node.ref, true);
        }
    }

    void StateMachineCanvas::DrawGrid(Frame &frame) const
    {
        auto *drawList = frame.drawList;
        const ImVec2 max = Add(frame.origin, frame.size);
        drawList->AddRectFilled(frame.origin, max, GetEditorColorU32(EditorColor::Background));
        const ImU32 minorColor = GetEditorColorU32(EditorColor::Border, 0.25f);
        const ImU32 majorColor = GetEditorColorU32(EditorColor::Border, 0.55f);
        const float step = frame.Scale(32.0f);
        if (step < 6.0f)
            return;
        const auto firstLine = [](float offset, float spacing) { return std::fmod(offset, spacing) - spacing; };
        int column = static_cast<int>(std::floor(-frame.camera.offset.x / step)) - 1;
        for (float x = frame.origin.x + firstLine(frame.camera.offset.x, step); x < max.x; x += step, ++column)
            drawList->AddLine(ImVec2(x, frame.origin.y), ImVec2(x, max.y), column % 5 == 0 ? majorColor : minorColor);
        int row = static_cast<int>(std::floor(-frame.camera.offset.y / step)) - 1;
        for (float y = frame.origin.y + firstLine(frame.camera.offset.y, step); y < max.y; y += step, ++row)
            drawList->AddLine(ImVec2(frame.origin.x, y), ImVec2(max.x, y), row % 5 == 0 ? majorColor : minorColor);
    }

    void StateMachineCanvas::DrawEdges(Frame &frame) const
    {
        const auto &view = frame.view;
        const auto &selection = frame.document.Selection();
        const bool anyNodeSelected = std::find(frame.selected.begin(), frame.selected.end(), true) != frame.selected.end();
        const float fontSize = ImGui::GetFontSize() * std::clamp(frame.camera.zoom, 0.7f, 1.0f) * 0.85f;

        for (std::size_t index = 0; index < view.edges.size(); ++index)
        {
            const auto &edge = view.edges[index];
            const auto &geometry = frame.edges[index];
            const bool edgeSelected = std::any_of(edge.transitionIds.begin(), edge.transitionIds.end(), [&selection](int id)
                                                  { return std::find(selection.transitionIds.begin(), selection.transitionIds.end(), id) !=
                                                           selection.transitionIds.end(); });
            const bool related = frame.selected[edge.from] || frame.selected[edge.to];
            const bool hovered = frame.hoveredEdge == static_cast<int>(index);
            const bool live = frame.liveEdge == static_cast<int>(index);
            const bool faded = frame.dimmed[edge.from] && frame.dimmed[edge.to];

            ImU32 color = edge.fromGroupSource ? kAnyStateColor : GetEditorColorU32(EditorColor::TextMuted);
            float alpha = 0.75f;
            float thickness = 1.5f;
            if (edgeSelected)
            {
                color = GetEditorColorU32(EditorColor::Accent);
                alpha = 1.0f;
                thickness = 3.0f;
            }
            else if (live)
            {
                color = GetEditorColorU32(EditorColor::Success);
                alpha = 1.0f;
                thickness = 3.0f;
            }
            else if (related)
            {
                color = edge.fromGroupSource ? kAnyStateColor : GetEditorColorU32(EditorColor::Accent);
                alpha = 0.95f;
                thickness = 2.0f;
            }
            else if (anyNodeSelected || faded)
                alpha = 0.18f;
            if (hovered)
            {
                alpha = 1.0f;
                thickness += 1.0f;
            }

            const ImU32 drawColor = WithAlpha(color, alpha);
            const float width = std::max(1.0f, frame.Scale(thickness));
            frame.drawList->AddLine(geometry.start, geometry.end, drawColor, width);

            // Mid-edge arrow keeps direction readable where many edges meet a node.
            const ImVec2 mid = Lerp(geometry.start, geometry.end, 0.5f);
            const float arrow = std::max(4.0f, frame.Scale(7.0f));
            const ImVec2 tip = Add(mid, Mul(geometry.direction, arrow));
            const ImVec2 back = Sub(mid, Mul(geometry.direction, arrow));
            frame.drawList->AddTriangleFilled(tip, Add(back, Mul(geometry.normal, arrow * 0.8f)),
                                              Sub(back, Mul(geometry.normal, arrow * 0.8f)), drawColor);

            if (edge.transitionIds.size() > 1 && frame.camera.zoom > 0.45f)
            {
                const std::string count = std::to_string(edge.transitionIds.size());
                const ImVec2 badgeCenter = Add(mid, Mul(geometry.normal, arrow + frame.Scale(8.0f)));
                const float radius = std::max(6.0f, frame.Scale(8.0f));
                frame.drawList->AddCircleFilled(badgeCenter, radius, GetEditorColorU32(EditorColor::Elevated, std::max(alpha, 0.6f)));
                frame.drawList->AddCircle(badgeCenter, radius, drawColor);
                const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, count.c_str());
                frame.drawList->AddText(ImGui::GetFont(), fontSize, Sub(badgeCenter, Mul(textSize, 0.5f)),
                                        GetEditorColorU32(EditorColor::Text, std::max(alpha, 0.6f)), count.c_str());
            }

            if (live && frame.overlay.live)
                frame.drawList->AddCircleFilled(Lerp(geometry.start, geometry.end, frame.overlay.live->transitionProgress),
                                                std::max(3.0f, frame.Scale(5.0f)), GetEditorColorU32(EditorColor::Success));
        }
    }

    void StateMachineCanvas::DrawNodes(Frame &frame) const
    {
        const auto &view = frame.view;
        auto *drawList = frame.drawList;
        const float zoom = frame.camera.zoom;
        const float titleSize = ImGui::GetFontSize() * zoom;
        const float subtitleSize = titleSize * 0.85f;

        for (std::size_t index = 0; index < view.nodes.size(); ++index)
        {
            const auto &node = view.nodes[index];
            const ImVec2 min = frame.NodeMin(index);
            const ImVec2 max = frame.NodeMax(index);
            const float alpha = frame.dimmed[index] ? 0.3f : 1.0f;
            const bool proxy = IsProxy(node);
            const float rounding = proxy ? (max.y - min.y) * 0.5f : frame.Scale(6.0f);
            const ImU32 kindColor = WithAlpha(KindColor(node), alpha);
            const bool live = frame.liveNode == static_cast<int>(index);
            const bool hovered = frame.hoveredNode == static_cast<int>(index);

            ImU32 fill = GetEditorColorU32(proxy ? EditorColor::Surface : EditorColor::Elevated, alpha);
            drawList->AddRectFilled(min, max, fill, rounding);
            if (live)
                drawList->AddRectFilled(min, max, GetEditorColorU32(EditorColor::Success, 0.18f * alpha), rounding);
            if (!proxy)
                drawList->AddRectFilled(min, ImVec2(min.x + frame.Scale(4.0f), max.y), kindColor, rounding, ImDrawFlags_RoundCornersLeft);

            ImU32 border = GetEditorColorU32(EditorColor::Border, alpha);
            float borderWidth = 1.0f;
            if (frame.selected[index])
            {
                border = GetEditorColorU32(EditorColor::Accent);
                borderWidth = 2.0f;
            }
            else if (live)
            {
                border = GetEditorColorU32(EditorColor::Success);
                borderWidth = 2.0f;
            }
            else if (proxy)
                border = kindColor;
            else if (hovered)
                border = GetEditorColorU32(EditorColor::TextMuted, alpha);
            drawList->AddRect(min, max, border, rounding, 0, std::max(1.0f, frame.Scale(borderWidth)));

            if (node.isEntry)
            {
                // Entry marker: a green arrowhead pointing into the node.
                const float midY = (min.y + max.y) * 0.5f;
                const float size = std::max(4.0f, frame.Scale(7.0f));
                drawList->AddTriangleFilled(ImVec2(min.x - frame.Scale(2.0f), midY), ImVec2(min.x - size * 1.6f, midY - size),
                                            ImVec2(min.x - size * 1.6f, midY + size), GetEditorColorU32(EditorColor::Success, alpha));
            }

            const float padding = frame.Scale(proxy ? 14.0f : 12.0f);
            float textWidth = max.x - min.x - padding * 2.0f;
            if (node.kind == ViewNodeKind::Group && titleSize >= 5.0f)
            {
                // A folder glyph marks nodes that open into another level.
                const float iconSize = titleSize * 1.1f;
                const ImVec2 iconExtent = ImGui::GetFont()->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icons::Folder);
                drawList->AddText(ImGui::GetFont(), iconSize,
                                  ImVec2(max.x - padding - iconExtent.x, (min.y + max.y - iconExtent.y) * 0.5f),
                                  WithAlpha(kGroupColor, 0.85f * alpha), icons::Folder);
                textWidth -= iconExtent.x + frame.Scale(6.0f);
            }
            const bool showSubtitle = zoom >= 0.5f && !node.subtitle.empty();
            const float textHeight = titleSize + (showSubtitle ? subtitleSize + frame.Scale(2.0f) : 0.0f);
            const float textTop = (min.y + max.y - textHeight) * 0.5f;
            DrawClippedText(drawList, titleSize, ImVec2(min.x + padding, textTop), textWidth,
                            proxy ? kindColor : GetEditorColorU32(EditorColor::Text, alpha), node.title);
            if (showSubtitle)
                DrawClippedText(drawList, subtitleSize, ImVec2(min.x + padding, textTop + titleSize + frame.Scale(2.0f)), textWidth,
                                GetEditorColorU32(EditorColor::TextMuted, alpha), node.subtitle);

            // Corner badges: problems first, then playback hints.
            if (frame.worstIssue[index] >= 0 && zoom >= 0.35f)
            {
                const bool error = frame.worstIssue[index] == static_cast<int>(assets::animgraph::DiagnosticSeverity::Error);
                const float radius = std::max(5.0f, frame.Scale(7.0f));
                const ImVec2 center(max.x - frame.Scale(2.0f), min.y + frame.Scale(2.0f));
                drawList->AddCircleFilled(center, radius, GetEditorColorU32(error ? EditorColor::Error : EditorColor::Warning));
                const ImVec2 bang = ImGui::GetFont()->CalcTextSizeA(radius * 1.6f, FLT_MAX, 0.0f, "!");
                drawList->AddText(ImGui::GetFont(), radius * 1.6f, Sub(center, Mul(bang, 0.5f)), IM_COL32(20, 20, 20, 255), "!");
            }
            if (node.kind == ViewNodeKind::State && zoom >= 0.6f)
            {
                const char *tag = node.scriptOnly ? "script" : (!node.loops ? "once" : nullptr);
                if (tag)
                {
                    const float tagSize = subtitleSize * 0.85f;
                    const ImVec2 tagExtent = ImGui::GetFont()->CalcTextSizeA(tagSize, FLT_MAX, 0.0f, tag);
                    drawList->AddText(ImGui::GetFont(), tagSize,
                                      ImVec2(max.x - tagExtent.x - frame.Scale(6.0f), min.y + frame.Scale(3.0f)),
                                      GetEditorColorU32(EditorColor::TextMuted, 0.8f * alpha), tag);
                }
            }

            if (live && frame.overlay.live)
            {
                const float barHeight = std::max(2.0f, frame.Scale(3.0f));
                const float progress = std::clamp(frame.overlay.live->activeStateProgress, 0.0f, 1.0f);
                drawList->AddRectFilled(ImVec2(min.x + rounding * 0.5f, max.y - barHeight - frame.Scale(2.0f)),
                                        ImVec2(min.x + rounding * 0.5f + (max.x - min.x - rounding) * progress, max.y - frame.Scale(2.0f)),
                                        GetEditorColorU32(EditorColor::Success));
            }

            // Connection handle on hover; dragging it creates a transition.
            if (hovered && ConnectionSource(node) && m_drag == DragMode::None)
            {
                const ImVec2 handle = frame.HandleCenter(index);
                const float radius = std::max(4.0f, frame.Scale(kHandleRadius));
                drawList->AddCircleFilled(handle, radius, frame.hoveredHandle ? GetEditorColorU32(EditorColor::Accent)
                                                                              : GetEditorColorU32(EditorColor::Elevated));
                drawList->AddCircle(handle, radius, GetEditorColorU32(EditorColor::Accent), 0, 1.5f);
            }
        }
    }

    void StateMachineCanvas::DrawConnectPreview(Frame &frame) const
    {
        if (!m_connectSource)
            return;
        int sourceIndex = -1;
        for (std::size_t index = 0; index < frame.view.nodes.size(); ++index)
            if (ConnectionSource(frame.view.nodes[index]) == m_connectSource)
                sourceIndex = static_cast<int>(index);
        if (sourceIndex < 0)
            return;

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        bool valid = false;
        ImVec2 end = mouse;
        if (frame.hoveredNode >= 0)
        {
            const auto target = ConnectionTarget(frame.view.nodes[static_cast<std::size_t>(frame.hoveredNode)]);
            valid = target && AnimationGraphDocument::CanConnect(frame.document.Graph(), *m_connectSource, *target);
            if (valid)
            {
                const auto index = static_cast<std::size_t>(frame.hoveredNode);
                frame.drawList->AddRect(frame.NodeMin(index), frame.NodeMax(index), GetEditorColorU32(EditorColor::Accent),
                                        frame.Scale(6.0f), 0, 2.0f);
                end = frame.NodeCenter(index);
            }
        }
        const ImVec2 start = frame.NodeCenter(static_cast<std::size_t>(sourceIndex));
        const ImU32 color = GetEditorColorU32(valid ? EditorColor::Accent : EditorColor::TextMuted);
        frame.drawList->AddLine(start, end, color, 2.0f);
        frame.drawList->AddCircleFilled(end, 4.0f, color);
    }

    void StateMachineCanvas::DrawBoxSelection(Frame &frame) const
    {
        if (m_drag != DragMode::BoxSelect || !ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragThreshold))
            return;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 min(std::min(m_dragStart.x, mouse.x), std::min(m_dragStart.y, mouse.y));
        const ImVec2 max(std::max(m_dragStart.x, mouse.x), std::max(m_dragStart.y, mouse.y));
        frame.drawList->AddRectFilled(min, max, GetEditorColorU32(EditorColor::Accent, 0.12f));
        frame.drawList->AddRect(min, max, GetEditorColorU32(EditorColor::Accent, 0.8f));
    }

    void StateMachineCanvas::DrawHoverTooltip(Frame &frame) const
    {
        if (m_drag != DragMode::None || m_connectSource || !ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            return;
        const auto &graph = frame.document.Graph();
        if (frame.hoveredEdge >= 0)
        {
            const auto &edge = frame.view.edges[static_cast<std::size_t>(frame.hoveredEdge)];
            ImGui::BeginTooltip();
            constexpr std::size_t kMaxListed = 10;
            for (std::size_t index = 0; index < edge.transitionIds.size() && index < kMaxListed; ++index)
            {
                const auto *transition = ag::FindTransition(graph, edge.transitionIds[index]);
                if (!transition)
                    continue;
                ImGui::Text("%s -> %s", ag::NodeName(graph, transition->from).c_str(), ag::NodeName(graph, transition->to).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("when %s", ag::DescribeTransitionGate(graph, *transition).c_str());
            }
            if (edge.transitionIds.size() > kMaxListed)
                ImGui::TextDisabled("...and %zu more", edge.transitionIds.size() - kMaxListed);
            ImGui::EndTooltip();
            return;
        }
        if (frame.hoveredNode < 0 || frame.hoveredHandle)
            return;

        const auto &node = frame.view.nodes[static_cast<std::size_t>(frame.hoveredNode)];
        std::string description;
        if (node.kind == ViewNodeKind::State)
        {
            if (const auto *state = ag::FindState(graph, node.ref.id))
                description = state->description;
        }
        else if (node.kind == ViewNodeKind::Group)
        {
            if (const auto *group = ag::FindGroup(graph, node.ref.id))
                description = group->description;
        }

        ImGui::BeginTooltip();
        switch (node.kind)
        {
        case ViewNodeKind::Group:
            ImGui::TextUnformatted("Double-click to open.");
            break;
        case ViewNodeKind::AnyState:
            ImGui::TextUnformatted("Transitions from here can fire from every state in this view.");
            break;
        case ViewNodeKind::Parent:
            ImGui::TextUnformatted("Transitions to or from states outside this group. Double-click to go up.");
            break;
        case ViewNodeKind::State:
            if (node.scriptOnly)
                ImGui::TextUnformatted("No transition enters this state; scripts play it by name.");
            break;
        }
        if (!description.empty())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextDisabled("%s", description.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTooltip();
    }

    void StateMachineCanvas::DrawContextMenu(Frame &frame)
    {
        auto &document = frame.document;
        const auto &view = frame.view;
        if (m_openContextMenu)
        {
            ImGui::OpenPopup("##StateMachineContext");
            m_openContextMenu = false;
        }
        if (!ImGui::BeginPopup("##StateMachineContext"))
            return;

        const auto &graph = document.Graph();
        const bool hasContent = !document.SelectedContent().empty();
        const auto moveToGroupMenu = [&]()
        {
            if (!ImGui::BeginMenu("Move to Group", hasContent))
                return;
            const auto selected = document.SelectedContent();
            if (view.groupId != assets::kAnimationGraphRootGroupId && ImGui::MenuItem("Root"))
                document.MoveNodesToGroup(selected, assets::kAnimationGraphRootGroupId);
            for (const auto &group : graph.groups)
            {
                if (group.id == view.groupId || document.Selection().Contains(NodeRef::ForGroup(group.id)))
                    continue;
                std::string label;
                for (const int pathId : ag::GroupPath(graph, group.id))
                    if (pathId != assets::kAnimationGraphRootGroupId)
                        label += (label.empty() ? "" : " / ") + ag::NodeName(graph, NodeRef::ForGroup(pathId));
                if (ImGui::MenuItem(label.c_str()))
                    document.MoveNodesToGroup(selected, group.id);
            }
            ImGui::EndMenu();
        };

        if (m_contextNode >= 0 && m_contextNode < static_cast<int>(view.nodes.size()))
        {
            const auto &node = view.nodes[static_cast<std::size_t>(m_contextNode)];
            switch (node.kind)
            {
            case ViewNodeKind::State:
                if (ImGui::MenuItem("Make Transition"))
                    BeginConnect(node.ref);
                if (ImGui::MenuItem(view.groupId == assets::kAnimationGraphRootGroupId ? "Set as Default State" : "Set as Group Entry",
                                    nullptr, false, !node.isEntry))
                    document.SetEntryState(node.ref.id);
                ImGui::Separator();
                moveToGroupMenu();
                if (ImGui::MenuItem("Group Selection", "Ctrl+G"))
                    document.GroupSelection();
                if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    document.DuplicateSelection();
                if (ImGui::MenuItem("Delete", "Del"))
                    document.DeleteSelection();
                break;
            case ViewNodeKind::Group:
                if (ImGui::MenuItem("Open"))
                    document.EnterGroup(node.ref.id);
                if (ImGui::MenuItem("Make Transition (from any state inside)"))
                    BeginConnect(node.ref);
                ImGui::Separator();
                moveToGroupMenu();
                if (ImGui::MenuItem("Ungroup"))
                    document.Ungroup(node.ref.id);
                if (ImGui::MenuItem("Delete Group and Contents", "Del"))
                    document.DeleteSelection();
                break;
            case ViewNodeKind::AnyState:
                if (ImGui::MenuItem("Make Transition"))
                    BeginConnect(node.ref);
                break;
            case ViewNodeKind::Parent:
                if (ImGui::MenuItem("Go Up"))
                    document.NavigateUp();
                break;
            }
        }
        else if (m_contextEdge >= 0 && m_contextEdge < static_cast<int>(view.edges.size()))
        {
            const auto ids = view.edges[static_cast<std::size_t>(m_contextEdge)].transitionIds;
            if (ImGui::MenuItem(ids.size() == 1 ? "Delete Transition" : "Delete Transitions"))
                document.DeleteTransitions(ids);
        }
        else
        {
            const ImVec2 at = m_contextCanvasPosition;
            if (ImGui::MenuItem("Add State"))
                document.AddState(at.x, at.y);
            if (ImGui::MenuItem("Add Blend Space State"))
                document.AddState(at.x, at.y, true);
            if (ImGui::MenuItem("Add Group"))
                document.AddGroup(at.x, at.y);
            ImGui::Separator();
            if (ImGui::MenuItem("Group Selection", "Ctrl+G", false, hasContent))
                document.GroupSelection();
            if (ImGui::MenuItem("Frame All", "F"))
                RequestFrameAll();
            if (ImGui::MenuItem("Go Up", "Backspace", false, view.groupId != assets::kAnimationGraphRootGroupId))
                document.NavigateUp();
        }
        ImGui::EndPopup();
    }
}
