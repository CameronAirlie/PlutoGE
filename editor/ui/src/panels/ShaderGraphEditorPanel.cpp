#include "PlutoGE/ui/panels/ShaderGraphEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kDefaultNodeWidth = 260.0f;
        constexpr float kNodeMinWidth = 180.0f;
        constexpr float kNodeMinHeight = 128.0f;
        constexpr float kCollapsedNodeHeight = 52.0f;
        constexpr float kResizeGripSize = 16.0f;
        constexpr float kSlotVerticalSpacing = 26.0f;

        constexpr const char *kPinOut[] = {"Out"};
        constexpr const char *kPinsBinary[] = {"A", "B"};
        constexpr const char *kPinsLerp[] = {"A", "B", "T"};
        constexpr const char *kPinsClamp[] = {"Value", "Min", "Max"};
        constexpr const char *kPinsValue[] = {"Value"};
        constexpr const char *kPinsNoise[] = {"UV", "Scale", "Strength"};
        constexpr const char *kPinsNoiseOutput[] = {"Value", "Color"};
        constexpr const char *kPinsOutput[] = {"Albedo", "Normal", "Metallic", "Roughness", "Opacity"};

        constexpr ImU32 kPinAnyColor = IM_COL32(170, 176, 188, 255);
        constexpr ImU32 kPinFloatColor = IM_COL32(230, 190, 92, 255);
        constexpr ImU32 kPinVec2Color = IM_COL32(84, 190, 210, 255);
        constexpr ImU32 kPinVec3Color = IM_COL32(96, 145, 230, 255);
        constexpr ImU32 kPinVec4Color = IM_COL32(214, 112, 206, 255);
        constexpr ImU32 kPinColorColor = IM_COL32(236, 118, 104, 255);

        ImU32 kPinOutAnyColors[] = {kPinAnyColor};
        ImU32 kPinOutFloatColors[] = {kPinFloatColor};
        ImU32 kPinOutVec2Colors[] = {kPinVec2Color};
        ImU32 kPinOutVec3Colors[] = {kPinVec3Color};
        ImU32 kPinOutVec4Colors[] = {kPinVec4Color};
        ImU32 kPinOutColorColors[] = {kPinColorColor};
        ImU32 kPinBinaryColors[] = {kPinAnyColor, kPinAnyColor};
        ImU32 kPinLerpColors[] = {kPinAnyColor, kPinAnyColor, kPinFloatColor};
        ImU32 kPinClampColors[] = {kPinAnyColor, kPinAnyColor, kPinAnyColor};
        ImU32 kPinNormalizeColors[] = {kPinVec3Color};
        ImU32 kPinNoiseInputColors[] = {kPinVec2Color, kPinFloatColor, kPinFloatColor};
        ImU32 kPinNoiseOutputColors[] = {kPinFloatColor, kPinColorColor};
        ImU32 kPinOutputColors[] = {kPinColorColor, kPinVec3Color, kPinFloatColor, kPinFloatColor, kPinFloatColor};

        const char **InputPins(render::ShaderGraphNodeKind kind, ImU8 &count)
        {
            switch (kind)
            {
            case render::ShaderGraphNodeKind::Add:
            case render::ShaderGraphNodeKind::Subtract:
            case render::ShaderGraphNodeKind::Multiply:
            case render::ShaderGraphNodeKind::Divide:
                count = 2;
                return const_cast<const char **>(kPinsBinary);
            case render::ShaderGraphNodeKind::Lerp:
                count = 3;
                return const_cast<const char **>(kPinsLerp);
            case render::ShaderGraphNodeKind::Clamp:
                count = 3;
                return const_cast<const char **>(kPinsClamp);
            case render::ShaderGraphNodeKind::Normalize:
                count = 1;
                return const_cast<const char **>(kPinsValue);
            case render::ShaderGraphNodeKind::NoiseTexture:
                count = 3;
                return const_cast<const char **>(kPinsNoise);
            case render::ShaderGraphNodeKind::Output:
                count = 5;
                return const_cast<const char **>(kPinsOutput);
            default:
                count = 0;
                return nullptr;
            }
        }

        const char **OutputPins(render::ShaderGraphNodeKind kind, ImU8 &count)
        {
            if (kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                count = 2;
                return const_cast<const char **>(kPinsNoiseOutput);
            }

            if (kind == render::ShaderGraphNodeKind::Output)
            {
                count = 0;
                return nullptr;
            }

            count = 1;
            return const_cast<const char **>(kPinOut);
        }

        const char *InputPinName(render::ShaderGraphNodeKind kind, GraphEditor::SlotIndex index)
        {
            ImU8 count = 0;
            const char **pins = InputPins(kind, count);
            return index < count ? pins[index] : "";
        }

        const char *OutputPinName(render::ShaderGraphNodeKind kind, GraphEditor::SlotIndex index)
        {
            ImU8 count = 0;
            const char **pins = OutputPins(kind, count);
            return index < count ? pins[index] : "";
        }

        ImU32 *InputPinColors(render::ShaderGraphNodeKind kind)
        {
            switch (kind)
            {
            case render::ShaderGraphNodeKind::Add:
            case render::ShaderGraphNodeKind::Subtract:
            case render::ShaderGraphNodeKind::Multiply:
            case render::ShaderGraphNodeKind::Divide:
                return kPinBinaryColors;
            case render::ShaderGraphNodeKind::Lerp:
                return kPinLerpColors;
            case render::ShaderGraphNodeKind::Clamp:
                return kPinClampColors;
            case render::ShaderGraphNodeKind::Normalize:
                return kPinNormalizeColors;
            case render::ShaderGraphNodeKind::NoiseTexture:
                return kPinNoiseInputColors;
            case render::ShaderGraphNodeKind::Output:
                return kPinOutputColors;
            default:
                return nullptr;
            }
        }

        ImU32 *OutputPinColors(render::ShaderGraphNodeKind kind)
        {
            switch (kind)
            {
            case render::ShaderGraphNodeKind::Float:
                return kPinOutFloatColors;
            case render::ShaderGraphNodeKind::Vec2:
            case render::ShaderGraphNodeKind::MeshUV:
                return kPinOutVec2Colors;
            case render::ShaderGraphNodeKind::Vec3:
                return kPinOutVec3Colors;
            case render::ShaderGraphNodeKind::Color:
                return kPinOutColorColors;
            case render::ShaderGraphNodeKind::NoiseTexture:
                return kPinNoiseOutputColors;
            case render::ShaderGraphNodeKind::MaterialInput:
            case render::ShaderGraphNodeKind::Add:
            case render::ShaderGraphNodeKind::Subtract:
            case render::ShaderGraphNodeKind::Multiply:
            case render::ShaderGraphNodeKind::Divide:
            case render::ShaderGraphNodeKind::Lerp:
            case render::ShaderGraphNodeKind::Clamp:
            case render::ShaderGraphNodeKind::Normalize:
                return kPinOutAnyColors;
            default:
                return nullptr;
            }
        }

        int PinCount(render::ShaderGraphNodeKind kind)
        {
            ImU8 inputCount = 0;
            ImU8 outputCount = 0;
            InputPins(kind, inputCount);
            OutputPins(kind, outputCount);
            return std::max(static_cast<int>(inputCount), static_cast<int>(outputCount));
        }

        float NodeHeight(render::ShaderGraphNodeKind kind)
        {
            return std::max(kNodeMinHeight, 42.0f + static_cast<float>(std::max(1, PinCount(kind))) * kSlotVerticalSpacing);
        }

        float NodeWidth(const render::ShaderGraphNode &node)
        {
            return std::max(kNodeMinWidth, node.size.x > 0.0f ? node.size.x : kDefaultNodeWidth);
        }

        float NodeHeight(const render::ShaderGraphNode &node)
        {
            if (node.collapsed)
            {
                return kCollapsedNodeHeight;
            }

            const float defaultHeight = NodeHeight(node.kind);
            return std::max(defaultHeight, node.size.y > 0.0f ? node.size.y : defaultHeight);
        }

        float MinimumNodeHeight(const render::ShaderGraphNode &node)
        {
            return node.collapsed ? kCollapsedNodeHeight : NodeHeight(node.kind);
        }

        float MeasureTextWidth(std::string_view text, float fontSize)
        {
            return ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
        }

        std::string EllipsizeText(std::string text, float maxWidth, float fontSize)
        {
            if (maxWidth <= 0.0f)
            {
                return {};
            }

            if (MeasureTextWidth(text, fontSize) <= maxWidth)
            {
                return text;
            }

            constexpr const char *ellipsis = "...";
            while (!text.empty() && MeasureTextWidth(text + ellipsis, fontSize) > maxWidth)
            {
                text.pop_back();
            }
            return text.empty() ? std::string(ellipsis) : text + ellipsis;
        }

        void DrawFittedText(ImDrawList *drawList, const ImVec2 &position, ImU32 color, std::string text, float maxWidth, float fontSize)
        {
            text = EllipsizeText(std::move(text), maxWidth, fontSize);
            if (text.empty())
            {
                return;
            }

            drawList->AddText(ImGui::GetFont(), fontSize, position, color, text.c_str());
        }

        GraphEditor::Template BuildTemplate(render::ShaderGraphNodeKind kind)
        {
            ImU8 inputCount = 0;
            ImU8 outputCount = 0;
            const char **inputs = InputPins(kind, inputCount);
            const char **outputs = OutputPins(kind, outputCount);
            ImU32 headerColor = IM_COL32(68, 86, 117, 255);
            ImU32 backgroundColor = IM_COL32(38, 44, 55, 255);
            ImU32 backgroundColorOver = IM_COL32(48, 57, 72, 255);

            if (kind == render::ShaderGraphNodeKind::Output)
            {
                headerColor = IM_COL32(108, 73, 45, 255);
                backgroundColor = IM_COL32(55, 44, 34, 255);
                backgroundColorOver = IM_COL32(72, 55, 40, 255);
            }
            else if (kind == render::ShaderGraphNodeKind::MaterialInput ||
                     kind == render::ShaderGraphNodeKind::MeshUV)
            {
                headerColor = IM_COL32(58, 102, 75, 255);
                backgroundColor = IM_COL32(35, 55, 43, 255);
                backgroundColorOver = IM_COL32(43, 70, 52, 255);
            }
            else if (kind == render::ShaderGraphNodeKind::Float ||
                     kind == render::ShaderGraphNodeKind::Vec2 ||
                     kind == render::ShaderGraphNodeKind::Vec3 ||
                     kind == render::ShaderGraphNodeKind::Color ||
                     kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                headerColor = IM_COL32(96, 76, 124, 255);
                backgroundColor = IM_COL32(49, 42, 61, 255);
                backgroundColorOver = IM_COL32(62, 52, 78, 255);
            }

            return GraphEditor::Template{
                headerColor,
                backgroundColor,
                backgroundColorOver,
                inputCount,
                inputs,
                InputPinColors(kind),
                outputCount,
                outputs,
                OutputPinColors(kind),
            };
        }

        int NextNodeId(const render::ShaderGraph &graph)
        {
            int id = 1;
            for (const auto &node : graph.nodes)
            {
                id = std::max(id, node.id + 1);
            }
            return id;
        }

        int NextLinkId(const render::ShaderGraph &graph)
        {
            int id = 1;
            for (const auto &link : graph.links)
            {
                id = std::max(id, link.id + 1);
            }
            return id;
        }

        render::ShaderGraphNode *FindNode(render::ShaderGraph &graph, int id)
        {
            const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                            [id](const render::ShaderGraphNode &node)
                                            {
                                                return node.id == id;
                                            });
            return found == graph.nodes.end() ? nullptr : &*found;
        }

        const render::ShaderGraphNode *FindNode(const render::ShaderGraph &graph, int id)
        {
            const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                            [id](const render::ShaderGraphNode &node)
                                            {
                                                return node.id == id;
                                            });
            return found == graph.nodes.end() ? nullptr : &*found;
        }

        bool IsSelected(const std::vector<int> &selectedNodeIds, int nodeId)
        {
            return std::find(selectedNodeIds.begin(), selectedNodeIds.end(), nodeId) != selectedNodeIds.end();
        }

        ImRect ResizeGripRect(const ImRect &nodeRect, float zoomFactor)
        {
            const float gripSize = std::max(10.0f, kResizeGripSize * zoomFactor);
            return ImRect(ImVec2(nodeRect.Max.x - gripSize, nodeRect.Max.y - gripSize),
                          ImVec2(nodeRect.Max.x + 2.0f * zoomFactor, nodeRect.Max.y + 2.0f * zoomFactor));
        }

        ImRect CollapseToggleRect(const ImRect &nodeRect, float zoomFactor)
        {
            const float toggleSize = std::max(10.0f, 15.0f * zoomFactor);
            return ImRect(ImVec2(nodeRect.Min.x + 4.0f * zoomFactor, nodeRect.Min.y + 2.0f * zoomFactor),
                          ImVec2(nodeRect.Min.x + 4.0f * zoomFactor + toggleSize, nodeRect.Min.y + 2.0f * zoomFactor + toggleSize));
        }

        std::size_t NodeIndexForId(const render::ShaderGraph &graph, int nodeId)
        {
            for (std::size_t index = 0; index < graph.nodes.size(); ++index)
            {
                if (graph.nodes[index].id == nodeId)
                {
                    return index;
                }
            }
            return static_cast<std::size_t>(-1);
        }

        GraphEditor::SlotIndex InputSlotForPin(const render::ShaderGraphNode &node, const std::string &pinName)
        {
            ImU8 count = 0;
            const char **pins = InputPins(node.kind, count);
            for (GraphEditor::SlotIndex index = 0; index < count; ++index)
            {
                if (pinName == pins[index])
                {
                    return index;
                }
            }
            return 0;
        }

        GraphEditor::SlotIndex OutputSlotForPin(const render::ShaderGraphNode &node, const std::string &pinName)
        {
            ImU8 count = 0;
            const char **pins = OutputPins(node.kind, count);
            for (GraphEditor::SlotIndex index = 0; index < count; ++index)
            {
                if (pinName == pins[index])
                {
                    return index;
                }
            }
            return 0;
        }

        bool CanCreateLink(const render::ShaderGraph &graph, std::size_t outputNodeIndex, std::size_t inputNodeIndex)
        {
            if (outputNodeIndex == inputNodeIndex ||
                outputNodeIndex >= graph.nodes.size() ||
                inputNodeIndex >= graph.nodes.size())
            {
                return false;
            }

            return graph.nodes[outputNodeIndex].kind != render::ShaderGraphNodeKind::Output;
        }

        class ShaderGraphDelegate : public GraphEditor::Delegate
        {
        public:
            ShaderGraphDelegate(render::ShaderGraph &graph,
                                std::vector<int> &selectedNodeIds,
                                int &selectedNodeId,
                                std::unordered_map<int, ImRect> &nodeScreenRects,
                                const GraphEditor::ViewState &viewState,
                                const ImVec2 &canvasScreenPos,
                                ImVec2 &addNodePosition,
                                int &resizingNodeId,
                                bool &openAddNodePopup,
                                bool &dirty)
                : m_graph(graph),
                  m_selectedNodeIds(selectedNodeIds),
                  m_selectedNodeId(selectedNodeId),
                  m_nodeScreenRects(nodeScreenRects),
                  m_viewState(viewState),
                  m_canvasScreenPos(canvasScreenPos),
                  m_addNodePosition(addNodePosition),
                  m_resizingNodeId(resizingNodeId),
                  m_openAddNodePopup(openAddNodePopup),
                  m_dirty(dirty)
            {
            }

            bool AllowedLink(GraphEditor::NodeIndex from, GraphEditor::NodeIndex to) override
            {
                return CanCreateLink(m_graph, to, from);
            }

            void SelectNode(GraphEditor::NodeIndex nodeIndex, bool selected) override
            {
                if (nodeIndex >= m_graph.nodes.size())
                {
                    return;
                }

                const int nodeId = m_graph.nodes[nodeIndex].id;
                if (selected && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (const auto rect = m_nodeScreenRects.find(nodeId); rect != m_nodeScreenRects.end())
                    {
                        const float zoomFactor = std::clamp((rect->second.GetWidth() + 8.0f) / NodeWidth(m_graph.nodes[nodeIndex]), 0.35f, 1.5f);
                        if (CollapseToggleRect(rect->second, zoomFactor).Contains(ImGui::GetIO().MouseClickedPos[0]))
                        {
                            auto &node = m_graph.nodes[nodeIndex];
                            node.collapsed = !node.collapsed;
                            if (node.size.x <= 0.0f)
                            {
                                node.size.x = kDefaultNodeWidth;
                            }
                            if (node.size.y <= 0.0f)
                            {
                                node.size.y = NodeHeight(node.kind);
                            }
                            m_dirty = true;
                        }
                    }
                }

                if (selected)
                {
                    if (!IsSelected(m_selectedNodeIds, nodeId))
                    {
                        m_selectedNodeIds.push_back(nodeId);
                    }
                    m_selectedNodeId = nodeId;
                }
                else
                {
                    m_selectedNodeIds.erase(std::remove(m_selectedNodeIds.begin(), m_selectedNodeIds.end(), nodeId), m_selectedNodeIds.end());
                    if (m_selectedNodeId == nodeId)
                    {
                        m_selectedNodeId = m_selectedNodeIds.empty() ? 0 : m_selectedNodeIds.back();
                    }
                }
            }

            void MoveSelectedNodes(const ImVec2 delta) override
            {
                const ImVec2 clickPosition = ImGui::GetIO().MouseClickedPos[0];
                if (m_resizingNodeId == 0)
                {
                    for (auto &node : m_graph.nodes)
                    {
                        if (!IsSelected(m_selectedNodeIds, node.id))
                        {
                            continue;
                        }

                        const auto rect = m_nodeScreenRects.find(node.id);
                        if (rect == m_nodeScreenRects.end())
                        {
                            continue;
                        }

                        const float zoomFactor = std::clamp((rect->second.GetWidth() + 8.0f) / NodeWidth(node), 0.35f, 1.5f);
                        if (ResizeGripRect(rect->second, zoomFactor).Contains(clickPosition))
                        {
                            m_resizingNodeId = node.id;
                            break;
                        }
                    }
                }

                if (m_resizingNodeId != 0)
                {
                    if (auto *node = FindNode(m_graph, m_resizingNodeId))
                    {
                        node->size.x = std::max(kNodeMinWidth, NodeWidth(*node) + delta.x);
                        if (!node->collapsed)
                        {
                            node->size.y = std::max(MinimumNodeHeight(*node), NodeHeight(*node) + delta.y);
                        }
                        else if (node->size.y <= 0.0f)
                        {
                            node->size.y = NodeHeight(node->kind);
                        }
                        m_dirty = true;
                    }
                    return;
                }

                for (auto &node : m_graph.nodes)
                {
                    if (!IsSelected(m_selectedNodeIds, node.id))
                    {
                        continue;
                    }
                    node.position.x += delta.x;
                    node.position.y += delta.y;
                    m_dirty = true;
                }
            }

            void AddLink(GraphEditor::NodeIndex inputNodeIndex,
                         GraphEditor::SlotIndex inputSlotIndex,
                         GraphEditor::NodeIndex outputNodeIndex,
                         GraphEditor::SlotIndex outputSlotIndex) override
            {
                if (!CanCreateLink(m_graph, inputNodeIndex, outputNodeIndex))
                {
                    return;
                }

                const auto &outputNode = m_graph.nodes[inputNodeIndex];
                const auto &inputNode = m_graph.nodes[outputNodeIndex];
                const std::string inputPin = InputPinName(inputNode.kind, outputSlotIndex);
                m_graph.links.erase(std::remove_if(m_graph.links.begin(), m_graph.links.end(),
                                                   [&inputNode, &inputPin](const render::ShaderGraphLink &link)
                                                   {
                                                       return link.toNodeId == inputNode.id && link.toPin == inputPin;
                                                   }),
                                    m_graph.links.end());
                m_graph.links.push_back(render::ShaderGraphLink{
                    .id = NextLinkId(m_graph),
                    .fromNodeId = outputNode.id,
                    .fromPin = OutputPinName(outputNode.kind, inputSlotIndex),
                    .toNodeId = inputNode.id,
                    .toPin = inputPin,
                });
                m_dirty = true;
            }

            void DelLink(GraphEditor::LinkIndex linkIndex) override
            {
                if (linkIndex >= m_graph.links.size())
                {
                    return;
                }
                m_graph.links.erase(m_graph.links.begin() + static_cast<std::ptrdiff_t>(linkIndex));
                m_dirty = true;
            }

            void CustomDraw(ImDrawList *drawList, ImRect rectangle, GraphEditor::NodeIndex nodeIndex) override
            {
                if (nodeIndex >= m_graph.nodes.size())
                {
                    return;
                }

                auto &node = m_graph.nodes[nodeIndex];
                const float zoomFactor = std::clamp((rectangle.GetWidth() + 8.0f) / NodeWidth(node), 0.35f, 1.5f);
                const float titleFontSize = std::max(8.0f, ImGui::GetFontSize() * zoomFactor);
                const float pinFontSize = std::max(7.0f, 13.0f * zoomFactor);
                const ImRect nodeRect(ImVec2(rectangle.Min.x - 3.0f, rectangle.Min.y - 23.0f),
                                      ImVec2(rectangle.Max.x + 3.0f, rectangle.Max.y + 3.0f));
                const ImVec2 titleMin = nodeRect.Min;
                const ImVec2 titleMax(nodeRect.Max.x, nodeRect.Min.y + 20.0f);
                const ImRect toggleRect = CollapseToggleRect(nodeRect, zoomFactor);
                const float toggleSize = toggleRect.GetWidth();
                m_nodeScreenRects[node.id] = nodeRect;

                const ImVec2 toggleCenter((toggleRect.Min.x + toggleRect.Max.x) * 0.5f,
                                          (toggleRect.Min.y + toggleRect.Max.y) * 0.5f);
                const float toggleHalf = toggleSize * 0.32f;
                drawList->AddRect(ImVec2(toggleCenter.x - toggleHalf, toggleCenter.y - toggleHalf),
                                  ImVec2(toggleCenter.x + toggleHalf, toggleCenter.y + toggleHalf),
                                  IM_COL32(210, 216, 226, 230),
                                  2.0f * zoomFactor);
                drawList->AddLine(ImVec2(toggleCenter.x - toggleHalf * 0.55f, toggleCenter.y),
                                  ImVec2(toggleCenter.x + toggleHalf * 0.55f, toggleCenter.y),
                                  IM_COL32(210, 216, 226, 230),
                                  1.2f * zoomFactor);
                if (node.collapsed)
                {
                    drawList->AddLine(ImVec2(toggleCenter.x, toggleCenter.y - toggleHalf * 0.55f),
                                      ImVec2(toggleCenter.x, toggleCenter.y + toggleHalf * 0.55f),
                                      IM_COL32(210, 216, 226, 230),
                                      1.2f * zoomFactor);
                }

                drawList->PushClipRect(titleMin, titleMax, true);
                DrawFittedText(drawList,
                               ImVec2(titleMin.x + 4.0f * zoomFactor + toggleSize + 6.0f * zoomFactor, titleMin.y + 2.0f * zoomFactor),
                               IM_COL32(238, 240, 245, 255),
                               node.name.empty() ? render::ToString(node.kind) : node.name,
                               std::max(0.0f, titleMax.x - titleMin.x - toggleSize - 16.0f * zoomFactor),
                               titleFontSize);
                drawList->PopClipRect();

                if (!node.collapsed)
                {
                    drawList->PushClipRect(rectangle.Min, rectangle.Max, true);
                    ImU8 inputCount = 0;
                    ImU8 outputCount = 0;
                    const char **inputPins = InputPins(node.kind, inputCount);
                    const char **outputPins = OutputPins(node.kind, outputCount);
                    const float nodeTop = rectangle.Min.y - 23.0f;
                    const float nodeHeight = rectangle.GetHeight() + 26.0f;
                    const float labelPadding = 16.0f * zoomFactor;
                    const float labelYAdjust = pinFontSize * 0.5f;
                    const float leftLabelX = rectangle.Min.x + labelPadding;
                    const float rightLimit = rectangle.Max.x - labelPadding;
                    const float inputLabelWidth = std::max(0.0f, rectangle.GetWidth() * 0.45f);
                    const float outputLabelWidth = std::max(0.0f, rectangle.GetWidth() * 0.45f);

                    for (ImU8 index = 0; index < inputCount; ++index)
                    {
                        const float y = nodeTop + nodeHeight * (static_cast<float>(index) + 1.0f) / (static_cast<float>(inputCount) + 1.0f) + 8.0f - labelYAdjust;
                        DrawFittedText(drawList, ImVec2(leftLabelX, y), IM_COL32(190, 198, 210, 255), inputPins[index], inputLabelWidth, pinFontSize);
                    }

                    for (ImU8 index = 0; index < outputCount; ++index)
                    {
                        std::string label = outputPins[index] ? outputPins[index] : "";
                        label = EllipsizeText(std::move(label), outputLabelWidth, pinFontSize);
                        const float textWidth = MeasureTextWidth(label, pinFontSize);
                        const float y = nodeTop + nodeHeight * (static_cast<float>(index) + 1.0f) / (static_cast<float>(outputCount) + 1.0f) + 8.0f - labelYAdjust;
                        drawList->AddText(ImGui::GetFont(), pinFontSize, ImVec2(std::max(rectangle.Min.x + labelPadding, rightLimit - textWidth), y), IM_COL32(190, 198, 210, 255), label.c_str());
                    }
                    drawList->PopClipRect();
                }

                const float gripSize = std::max(10.0f, kResizeGripSize * zoomFactor);
                const ImRect gripRect = ResizeGripRect(nodeRect, zoomFactor);
                const ImVec2 gripMin = gripRect.Min;
                const ImVec2 gripMax = gripRect.Max;
                drawList->AddTriangleFilled(ImVec2(gripMax.x, gripMax.y),
                                            ImVec2(gripMin.x, gripMax.y),
                                            ImVec2(gripMax.x, gripMin.y),
                                            IM_COL32(210, 216, 226, 120));
                drawList->AddLine(ImVec2(gripMax.x - gripSize * 0.35f, gripMax.y - 2.0f * zoomFactor),
                                  ImVec2(gripMax.x - 2.0f * zoomFactor, gripMax.y - gripSize * 0.35f),
                                  IM_COL32(210, 216, 226, 190),
                                  1.0f * zoomFactor);
            }

            void RightClick(GraphEditor::NodeIndex nodeIndex, GraphEditor::SlotIndex, GraphEditor::SlotIndex) override
            {
                if (nodeIndex < m_graph.nodes.size())
                {
                    m_selectedNodeId = m_graph.nodes[nodeIndex].id;
                    return;
                }

                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float factor = std::max(0.001f, m_viewState.mFactor);
                m_addNodePosition = ImVec2((mouse.x - m_canvasScreenPos.x) / factor - m_viewState.mPosition.x,
                                           (mouse.y - m_canvasScreenPos.y) / factor - m_viewState.mPosition.y);
                m_openAddNodePopup = true;
            }

            const size_t GetTemplateCount() override
            {
                return m_templates.size();
            }

            const GraphEditor::Template GetTemplate(GraphEditor::TemplateIndex index) override
            {
                return m_templates[index];
            }

            const size_t GetNodeCount() override
            {
                return m_graph.nodes.size();
            }

            const GraphEditor::Node GetNode(GraphEditor::NodeIndex index) override
            {
                const auto &node = m_graph.nodes[index];
                return GraphEditor::Node{
                    "",
                    TemplateIndexForNode(node),
                    ImRect(ImVec2(node.position.x, node.position.y),
                           ImVec2(node.position.x + NodeWidth(node), node.position.y + NodeHeight(node))),
                    IsSelected(m_selectedNodeIds, node.id),
                };
            }

            const size_t GetLinkCount() override
            {
                return m_graph.links.size();
            }

            const GraphEditor::Link GetLink(GraphEditor::LinkIndex index) override
            {
                const auto &link = m_graph.links[index];
                const auto inputIndex = NodeIndexForId(m_graph, link.toNodeId);
                const auto outputIndex = NodeIndexForId(m_graph, link.fromNodeId);
                const auto *inputNode = FindNode(m_graph, link.toNodeId);
                const auto *outputNode = FindNode(m_graph, link.fromNodeId);
                return GraphEditor::Link{
                    outputIndex,
                    outputNode ? OutputSlotForPin(*outputNode, link.fromPin) : 0,
                    inputIndex,
                    inputNode ? InputSlotForPin(*inputNode, link.toPin) : 0,
                };
            }

        private:
            GraphEditor::TemplateIndex TemplateIndexForNode(const render::ShaderGraphNode &node) const
            {
                return static_cast<GraphEditor::TemplateIndex>(std::clamp(static_cast<int>(node.kind), 0, static_cast<int>(m_templates.size() - 1)));
            }

            static const std::array<GraphEditor::Template, 15> m_templates;

            render::ShaderGraph &m_graph;
            std::vector<int> &m_selectedNodeIds;
            int &m_selectedNodeId;
            std::unordered_map<int, ImRect> &m_nodeScreenRects;
            const GraphEditor::ViewState &m_viewState;
            ImVec2 m_canvasScreenPos;
            ImVec2 &m_addNodePosition;
            int &m_resizingNodeId;
            bool &m_openAddNodePopup;
            bool &m_dirty;
        };

        const std::array<GraphEditor::Template, 15> ShaderGraphDelegate::m_templates = {
            BuildTemplate(render::ShaderGraphNodeKind::MaterialInput),
            BuildTemplate(render::ShaderGraphNodeKind::Float),
            BuildTemplate(render::ShaderGraphNodeKind::Vec2),
            BuildTemplate(render::ShaderGraphNodeKind::Vec3),
            BuildTemplate(render::ShaderGraphNodeKind::Color),
            BuildTemplate(render::ShaderGraphNodeKind::Add),
            BuildTemplate(render::ShaderGraphNodeKind::Subtract),
            BuildTemplate(render::ShaderGraphNodeKind::Multiply),
            BuildTemplate(render::ShaderGraphNodeKind::Divide),
            BuildTemplate(render::ShaderGraphNodeKind::Lerp),
            BuildTemplate(render::ShaderGraphNodeKind::Clamp),
            BuildTemplate(render::ShaderGraphNodeKind::Normalize),
            BuildTemplate(render::ShaderGraphNodeKind::NoiseTexture),
            BuildTemplate(render::ShaderGraphNodeKind::MeshUV),
            BuildTemplate(render::ShaderGraphNodeKind::Output),
        };

        bool IsOutputNode(const render::ShaderGraphNode &node)
        {
            return node.kind == render::ShaderGraphNodeKind::Output;
        }

        render::ShaderGraphNode MakeNode(render::ShaderGraph &graph, render::ShaderGraphNodeKind kind, const char *name)
        {
            render::ShaderGraphNode node{
                .id = NextNodeId(graph),
                .kind = kind,
                .name = name,
                .position = {60.0f + static_cast<float>(graph.nodes.size() % 5) * 34.0f,
                             60.0f + static_cast<float>(graph.nodes.size() % 7) * 28.0f},
                .size = {kDefaultNodeWidth, NodeHeight(kind)},
            };

            if (kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                node.value.x = 8.0f;
                node.value.y = 1.0f;
            }

            return node;
        }

        render::ShaderGraphNode MakeNodeAt(render::ShaderGraph &graph, render::ShaderGraphNodeKind kind, const char *name, const ImVec2 &position)
        {
            render::ShaderGraphNode node = MakeNode(graph, kind, name);
            node.position = {position.x, position.y};
            return node;
        }

        bool AddNodeMenuItem(const char *label, render::ShaderGraph &graph, render::ShaderGraphNodeKind kind, const char *name, const ImVec2 &position, bool &dirty)
        {
            if (!ImGui::MenuItem(label))
            {
                return false;
            }

            graph.nodes.push_back(MakeNodeAt(graph, kind, name, position));
            dirty = true;
            return true;
        }
    }

    void ShaderGraphEditorPanel::LoadActiveGraph()
    {
        auto &editorShell = EditorShell::GetInstance();
        m_loadedReference = editorShell.GetActiveShaderGraphAssetReference();
        bool loaded = false;
        m_graph = core::Engine::GetInstance().GetAssetManager().LoadShaderGraphAsset(m_loadedReference, &loaded);
        if (!loaded)
        {
            m_graph = render::CreateDefaultShaderGraph();
        }
        m_selectedNodeId = m_graph.nodes.empty() ? 0 : m_graph.nodes.front().id;
        m_selectedNodeIds.clear();
        if (m_selectedNodeId != 0)
        {
            m_selectedNodeIds.push_back(m_selectedNodeId);
        }
        m_graphFit = GraphEditor::Fit_AllNodes;
        m_nodeScreenRects.clear();
        m_openAddNodePopup = false;
        m_resizingNodeId = 0;
        m_dirty = false;
    }

    void ShaderGraphEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveShaderGraphAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No shader graph selected.");
            return;
        }

        if (reference != m_loadedReference)
        {
            LoadActiveGraph();
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_resizingNodeId = 0;
        }

        m_graphOptions.mMinimap = ImRect(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f));
        m_graphOptions.mNodeSlotRadius = 5.5f;
        m_graphOptions.mLineThickness = 3.0f;
        m_graphOptions.mBorderThickness = 2.0f;
        m_graphOptions.mBorderSelectionThickness = 3.0f;
        m_graphOptions.mDrawIONameOnHover = false;
        m_graphOptions.mDisplayLinksAsCurves = false;

        const bool engineGraph = assets::Project::IsEngineAssetReference(reference);
        ImGui::TextWrapped("Shader Graph: %s", reference.c_str());
        if (engineGraph)
        {
            ImGui::TextDisabled("Engine shader graph assets are read-only.");
        }

        ImGui::BeginDisabled(engineGraph);
        if (ImGui::Button("Fit"))
        {
            m_graphFit = GraphEditor::Fit_AllNodes;
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Columns(2, "ShaderGraphEditorColumns", true);

        const ImVec2 graphSize(ImGui::GetContentRegionAvail().x, std::max(360.0f, ImGui::GetContentRegionAvail().y - 62.0f));
        ImGui::BeginChild("ShaderGraphCanvasHost", graphSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 canvasScreenPos = ImGui::GetCursorScreenPos();
        ShaderGraphDelegate delegate(m_graph,
                                     m_selectedNodeIds,
                                     m_selectedNodeId,
                                     m_nodeScreenRects,
                                     m_graphViewState,
                                     canvasScreenPos,
                                     m_addNodePosition,
                                     m_resizingNodeId,
                                     m_openAddNodePopup,
                                     m_dirty);
        GraphEditor::Show(delegate, m_graphOptions, m_graphViewState, !engineGraph, &m_graphFit);
        if (m_openAddNodePopup)
        {
            ImGui::OpenPopup("ShaderGraphAddNodePopup");
            m_openAddNodePopup = false;
        }
        ImGui::BeginDisabled(engineGraph);
        if (ImGui::BeginPopup("ShaderGraphAddNodePopup"))
        {
            if (ImGui::BeginMenu("Inputs"))
            {
                AddNodeMenuItem("Material Input", m_graph, render::ShaderGraphNodeKind::MaterialInput, "Material Input", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Mesh UV", m_graph, render::ShaderGraphNodeKind::MeshUV, "Mesh UV", m_addNodePosition, m_dirty);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Values"))
            {
                AddNodeMenuItem("Float", m_graph, render::ShaderGraphNodeKind::Float, "Float", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Vec2", m_graph, render::ShaderGraphNodeKind::Vec2, "Vec2", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Vec3", m_graph, render::ShaderGraphNodeKind::Vec3, "Vec3", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Color", m_graph, render::ShaderGraphNodeKind::Color, "Color", m_addNodePosition, m_dirty);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Textures"))
            {
                AddNodeMenuItem("Noise Texture", m_graph, render::ShaderGraphNodeKind::NoiseTexture, "Noise Texture", m_addNodePosition, m_dirty);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Math"))
            {
                AddNodeMenuItem("Add", m_graph, render::ShaderGraphNodeKind::Add, "Add", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Subtract", m_graph, render::ShaderGraphNodeKind::Subtract, "Subtract", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Multiply", m_graph, render::ShaderGraphNodeKind::Multiply, "Multiply", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Divide", m_graph, render::ShaderGraphNodeKind::Divide, "Divide", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Lerp", m_graph, render::ShaderGraphNodeKind::Lerp, "Lerp", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Clamp", m_graph, render::ShaderGraphNodeKind::Clamp, "Clamp", m_addNodePosition, m_dirty);
                AddNodeMenuItem("Normalize", m_graph, render::ShaderGraphNodeKind::Normalize, "Normalize", m_addNodePosition, m_dirty);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Output"))
            {
                AddNodeMenuItem("Geometry Output", m_graph, render::ShaderGraphNodeKind::Output, "Geometry Output", m_addNodePosition, m_dirty);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::NextColumn();
        ImGui::TextUnformatted("Inspector");
        render::ShaderGraphNode *selectedNode = FindNode(m_graph, m_selectedNodeId);
        if (selectedNode)
        {
            ImGui::BeginDisabled(engineGraph);
            char nameBuffer[128]{};
            strncpy_s(nameBuffer, selectedNode->name.c_str(), _TRUNCATE);
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
            {
                selectedNode->name = nameBuffer;
                m_dirty = true;
            }

            float position[2] = {selectedNode->position.x, selectedNode->position.y};
            if (ImGui::DragFloat2("Position", position, 1.0f))
            {
                selectedNode->position = {position[0], position[1]};
                m_dirty = true;
            }

            bool collapsed = selectedNode->collapsed;
            if (ImGui::Checkbox("Collapsed", &collapsed))
            {
                selectedNode->collapsed = collapsed;
                if (selectedNode->size.x <= 0.0f)
                {
                    selectedNode->size.x = kDefaultNodeWidth;
                }
                if (selectedNode->size.y <= 0.0f)
                {
                    selectedNode->size.y = NodeHeight(selectedNode->kind);
                }
                m_dirty = true;
            }

            float size[2] = {NodeWidth(*selectedNode), selectedNode->collapsed ? std::max(NodeHeight(selectedNode->kind), selectedNode->size.y) : NodeHeight(*selectedNode)};
            if (ImGui::DragFloat2("Size", size, 1.0f, 0.0f, 0.0f, "%.0f"))
            {
                selectedNode->size.x = std::max(kNodeMinWidth, size[0]);
                selectedNode->size.y = std::max(NodeHeight(selectedNode->kind), size[1]);
                m_dirty = true;
            }

            if (selectedNode->kind == render::ShaderGraphNodeKind::MaterialInput)
            {
                int materialInput = static_cast<int>(selectedNode->materialInput);
                const char *items[] = {"Color", "Normal", "Metallic", "Roughness", "Opacity", "UV"};
                if (ImGui::Combo("Input", &materialInput, items, IM_ARRAYSIZE(items)))
                {
                    selectedNode->materialInput = static_cast<render::ShaderGraphMaterialInput>(materialInput);
                    m_dirty = true;
                }
            }
            else if (selectedNode->kind == render::ShaderGraphNodeKind::Float)
            {
                if (ImGui::DragFloat("Value", &selectedNode->value.x, 0.01f))
                {
                    m_dirty = true;
                }
            }
            else if (selectedNode->kind == render::ShaderGraphNodeKind::Vec2)
            {
                if (ImGui::DragFloat2("Value", &selectedNode->value.x, 0.01f))
                {
                    m_dirty = true;
                }
            }
            else if (selectedNode->kind == render::ShaderGraphNodeKind::Vec3)
            {
                if (ImGui::DragFloat3("Value", &selectedNode->value.x, 0.01f))
                {
                    m_dirty = true;
                }
            }
            else if (selectedNode->kind == render::ShaderGraphNodeKind::Color)
            {
                if (ImGui::ColorEdit4("Value", &selectedNode->value.x))
                {
                    m_dirty = true;
                }
            }
            else if (selectedNode->kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                if (ImGui::DragFloat("Scale", &selectedNode->value.x, 0.1f, 0.01f, 512.0f))
                {
                    m_dirty = true;
                }
                if (ImGui::DragFloat("Strength", &selectedNode->value.y, 0.01f, 0.0f, 8.0f))
                {
                    m_dirty = true;
                }
            }

            ImGui::BeginDisabled(IsOutputNode(*selectedNode));
            if (ImGui::Button("Delete Node"))
            {
                const int removedId = selectedNode->id;
                m_graph.nodes.erase(std::remove_if(m_graph.nodes.begin(), m_graph.nodes.end(),
                                                   [removedId](const render::ShaderGraphNode &node)
                                                   {
                                                       return node.id == removedId;
                                                   }),
                                    m_graph.nodes.end());
                m_graph.links.erase(std::remove_if(m_graph.links.begin(), m_graph.links.end(),
                                                   [removedId](const render::ShaderGraphLink &link)
                                                   {
                                                       return link.fromNodeId == removedId || link.toNodeId == removedId;
                                                   }),
                                    m_graph.links.end());
                m_selectedNodeIds.erase(std::remove(m_selectedNodeIds.begin(), m_selectedNodeIds.end(), removedId), m_selectedNodeIds.end());
                m_selectedNodeId = m_graph.nodes.empty() ? 0 : m_graph.nodes.front().id;
                m_dirty = true;
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextDisabled("Select a node in the graph.");
        }

        ImGui::Columns(1);

        ImGui::Separator();
        ImGui::BeginDisabled(engineGraph || !m_dirty);
        if (ImGui::Button("Save"))
        {
            std::string errorMessage;
            if (core::Engine::GetInstance().GetAssetManager().SaveShaderGraphAsset(reference, m_graph, &errorMessage))
            {
                m_dirty = false;
                editorShell.MarkProjectDirty();
                editorShell.MarkSceneDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved shader graph: " + reference);
            }
            else
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save shader graph." : errorMessage);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Revert"))
        {
            LoadActiveGraph();
        }
        ImGui::EndDisabled();
    }
}
