#include "PlutoGE/ui/panels/AnimationGraphEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kStateNodeWidth = 190.0f;
        constexpr float kStateNodeHeight = 92.0f;

        struct ClipAssetOption
        {
            std::string reference;
            std::string displayName;
        };

        bool StartsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        std::vector<ClipAssetOption> CollectClipAssetOptions(const assets::Project *project)
        {
            std::vector<ClipAssetOption> options;
            if (!project)
            {
                return options;
            }

            for (const auto &asset : project->GetManifest().assetEntries)
            {
                if (asset.type != assets::ProjectAssetType::AnimationClip)
                {
                    continue;
                }

                std::string displayName = asset.reference;
                if (StartsWith(displayName, assets::Project::kProjectAssetScheme))
                {
                    displayName.erase(0, assets::Project::kProjectAssetScheme.size());
                }
                options.push_back(ClipAssetOption{.reference = asset.reference, .displayName = std::move(displayName)});
            }

            std::sort(options.begin(), options.end(),
                      [](const ClipAssetOption &left, const ClipAssetOption &right)
                      {
                          return left.displayName < right.displayName;
                      });
            return options;
        }

        std::string ClipNameFromReference(const std::string &reference)
        {
            if (reference.empty())
            {
                return {};
            }

            std::string filename = reference;
            const auto slash = filename.find_last_of("/\\");
            if (slash != std::string::npos)
            {
                filename = filename.substr(slash + 1);
            }
            const auto dot = filename.find_last_of('.');
            if (dot != std::string::npos)
            {
                filename.erase(dot);
            }
            return filename;
        }

        const char *ParameterTypeLabel(assets::AnimationGraphParameterType type)
        {
            switch (type)
            {
            case assets::AnimationGraphParameterType::Int:
                return "Int";
            case assets::AnimationGraphParameterType::Bool:
                return "Bool";
            case assets::AnimationGraphParameterType::Trigger:
                return "Trigger";
            case assets::AnimationGraphParameterType::Float:
            default:
                return "Float";
            }
        }

        const char *ConditionModeLabel(assets::AnimationGraphConditionMode mode)
        {
            switch (mode)
            {
            case assets::AnimationGraphConditionMode::IfNot:
                return "If Not";
            case assets::AnimationGraphConditionMode::Greater:
                return "Greater";
            case assets::AnimationGraphConditionMode::Less:
                return "Less";
            case assets::AnimationGraphConditionMode::Equals:
                return "Equals";
            case assets::AnimationGraphConditionMode::NotEqual:
                return "Not Equal";
            case assets::AnimationGraphConditionMode::If:
            default:
                return "If";
            }
        }

        int NextStateId(const assets::AnimationGraphAsset &graph)
        {
            int id = 1;
            for (const auto &state : graph.states)
            {
                id = std::max(id, state.id + 1);
            }
            return id;
        }

        int NextTransitionId(const assets::AnimationGraphAsset &graph)
        {
            int id = 1;
            for (const auto &transition : graph.transitions)
            {
                id = std::max(id, transition.id + 1);
            }
            return id;
        }

        int NextParameterId(const assets::AnimationGraphAsset &graph)
        {
            int id = 1;
            for (const auto &parameter : graph.parameters)
            {
                id = std::max(id, parameter.id + 1);
            }
            return id;
        }

        std::size_t StateIndexForId(const assets::AnimationGraphAsset &graph, int id)
        {
            for (std::size_t index = 0; index < graph.states.size(); ++index)
            {
                if (graph.states[index].id == id)
                {
                    return index;
                }
            }
            return graph.states.size();
        }

        assets::AnimationGraphState *FindState(assets::AnimationGraphAsset &graph, int id)
        {
            const auto index = StateIndexForId(graph, id);
            return index < graph.states.size() ? &graph.states[index] : nullptr;
        }

        assets::AnimationGraphTransition *FindTransition(assets::AnimationGraphAsset &graph, int id)
        {
            for (auto &transition : graph.transitions)
            {
                if (transition.id == id)
                {
                    return &transition;
                }
            }
            return nullptr;
        }

        void RemoveState(assets::AnimationGraphAsset &graph, int stateId)
        {
            graph.states.erase(std::remove_if(graph.states.begin(), graph.states.end(),
                                              [stateId](const assets::AnimationGraphState &state)
                                              {
                                                  return state.id == stateId;
                                              }),
                               graph.states.end());
            graph.transitions.erase(std::remove_if(graph.transitions.begin(), graph.transitions.end(),
                                                   [stateId](const assets::AnimationGraphTransition &transition)
                                                   {
                                                       return transition.fromStateId == stateId || transition.toStateId == stateId;
                                                   }),
                                    graph.transitions.end());
            if (graph.defaultStateId == stateId)
            {
                graph.defaultStateId = graph.states.empty() ? 0 : graph.states.front().id;
            }
        }

        bool HasTransition(const assets::AnimationGraphAsset &graph, int fromStateId, int toStateId)
        {
            return std::any_of(graph.transitions.begin(), graph.transitions.end(),
                               [fromStateId, toStateId](const assets::AnimationGraphTransition &transition)
                               {
                                   return transition.fromStateId == fromStateId && transition.toStateId == toStateId;
                               });
        }

        GraphEditor::Template BuildStateTemplate()
        {
            static const char *inputs[] = {"In"};
            static const char *outputs[] = {"Out"};
            static ImU32 inputColors[] = {IM_COL32(125, 186, 230, 255)};
            static ImU32 outputColors[] = {IM_COL32(232, 179, 94, 255)};
            return GraphEditor::Template{
                IM_COL32(53, 91, 111, 255),
                IM_COL32(35, 47, 55, 255),
                IM_COL32(45, 61, 72, 255),
                1,
                inputs,
                inputColors,
                1,
                outputs,
                outputColors,
            };
        }

        class AnimationGraphDelegate : public GraphEditor::Delegate
        {
        public:
            AnimationGraphDelegate(assets::AnimationGraphAsset &graph,
                                   int &selectedStateId,
                                   std::vector<int> &selectedStateIds,
                                   int &selectedTransitionId,
                                   const GraphEditor::ViewState &viewState,
                                   const ImVec2 &canvasScreenPos,
                                   ImVec2 &addStatePosition,
                                   bool &openAddStatePopup,
                                   bool &dirty)
                : m_graph(graph),
                  m_selectedStateId(selectedStateId),
                  m_selectedStateIds(selectedStateIds),
                  m_selectedTransitionId(selectedTransitionId),
                  m_viewState(viewState),
                  m_canvasScreenPos(canvasScreenPos),
                  m_addStatePosition(addStatePosition),
                  m_openAddStatePopup(openAddStatePopup),
                  m_dirty(dirty)
            {
            }

            bool AllowedLink(GraphEditor::NodeIndex from, GraphEditor::NodeIndex to) override
            {
                const GraphEditor::NodeIndex source = to;
                const GraphEditor::NodeIndex destination = from;
                return source < m_graph.states.size() &&
                       destination < m_graph.states.size() &&
                       source != destination &&
                       !HasTransition(m_graph, m_graph.states[source].id, m_graph.states[destination].id);
            }

            void SelectNode(GraphEditor::NodeIndex nodeIndex, bool selected) override
            {
                if (nodeIndex >= m_graph.states.size())
                {
                    return;
                }

                const int stateId = m_graph.states[nodeIndex].id;
                if (selected)
                {
                    if (std::find(m_selectedStateIds.begin(), m_selectedStateIds.end(), stateId) == m_selectedStateIds.end())
                    {
                        m_selectedStateIds.push_back(stateId);
                    }
                    m_selectedStateId = stateId;
                    m_selectedTransitionId = 0;
                }
                else
                {
                    m_selectedStateIds.erase(std::remove(m_selectedStateIds.begin(), m_selectedStateIds.end(), stateId), m_selectedStateIds.end());
                    if (m_selectedStateId == stateId)
                    {
                        m_selectedStateId = m_selectedStateIds.empty() ? 0 : m_selectedStateIds.back();
                    }
                }
            }

            void MoveSelectedNodes(const ImVec2 delta) override
            {
                for (auto &state : m_graph.states)
                {
                    if (std::find(m_selectedStateIds.begin(), m_selectedStateIds.end(), state.id) == m_selectedStateIds.end())
                    {
                        continue;
                    }

                    state.positionX += delta.x;
                    state.positionY += delta.y;
                    m_dirty = true;
                }
            }

            void AddLink(GraphEditor::NodeIndex inputNodeIndex,
                         GraphEditor::SlotIndex,
                         GraphEditor::NodeIndex outputNodeIndex,
                         GraphEditor::SlotIndex) override
            {
                if (inputNodeIndex >= m_graph.states.size() ||
                    outputNodeIndex >= m_graph.states.size() ||
                    inputNodeIndex == outputNodeIndex ||
                    HasTransition(m_graph, m_graph.states[inputNodeIndex].id, m_graph.states[outputNodeIndex].id))
                {
                    return;
                }

                m_graph.transitions.push_back(assets::AnimationGraphTransition{
                    .id = NextTransitionId(m_graph),
                    .fromStateId = m_graph.states[inputNodeIndex].id,
                    .toStateId = m_graph.states[outputNodeIndex].id,
                    .duration = 0.15f,
                });
                m_selectedTransitionId = m_graph.transitions.back().id;
                m_dirty = true;
            }

            void DelLink(GraphEditor::LinkIndex linkIndex) override
            {
                if (linkIndex >= m_graph.transitions.size())
                {
                    return;
                }
                const int removedId = m_graph.transitions[linkIndex].id;
                m_graph.transitions.erase(m_graph.transitions.begin() + static_cast<std::ptrdiff_t>(linkIndex));
                if (m_selectedTransitionId == removedId)
                {
                    m_selectedTransitionId = 0;
                }
                m_dirty = true;
            }

            void CustomDraw(ImDrawList *drawList, ImRect rectangle, GraphEditor::NodeIndex nodeIndex) override
            {
                if (nodeIndex >= m_graph.states.size())
                {
                    return;
                }

                const auto &state = m_graph.states[nodeIndex];
                const ImRect nodeRect(ImVec2(rectangle.Min.x - 3.0f, rectangle.Min.y - 23.0f),
                                      ImVec2(rectangle.Max.x + 3.0f, rectangle.Max.y + 3.0f));
                const ImVec2 titleMin = nodeRect.Min;
                const ImVec2 titleMax(nodeRect.Max.x, nodeRect.Min.y + 22.0f);
                const bool isDefault = state.id == m_graph.defaultStateId;
                drawList->AddRectFilled(titleMin, titleMax, isDefault ? IM_COL32(72, 112, 80, 255) : IM_COL32(53, 91, 111, 255), 5.0f, ImDrawFlags_RoundCornersTop);
                drawList->AddText(ImVec2(titleMin.x + 8.0f, titleMin.y + 3.0f),
                                  IM_COL32(238, 240, 245, 255),
                                  state.name.empty() ? "State" : state.name.c_str());
                if (isDefault)
                {
                    drawList->AddText(ImVec2(titleMax.x - 54.0f, titleMin.y + 3.0f), IM_COL32(196, 238, 186, 255), "Default");
                }

                drawList->AddText(ImVec2(rectangle.Min.x + 12.0f, rectangle.Min.y + 8.0f),
                                  IM_COL32(190, 198, 210, 255),
                                  "In");
                drawList->AddText(ImVec2(rectangle.Max.x - 38.0f, rectangle.Min.y + 8.0f),
                                  IM_COL32(190, 198, 210, 255),
                                  "Out");

                std::string clip = state.clipName.empty() ? "Clip #" + std::to_string(state.clipIndex) : state.clipName;
                if (clip.size() > 22)
                {
                    clip.resize(19);
                    clip += "...";
                }
                drawList->AddText(ImVec2(rectangle.Min.x + 12.0f, rectangle.Min.y + 38.0f),
                                  IM_COL32(165, 174, 188, 255),
                                  clip.c_str());
            }

            void RightClick(GraphEditor::NodeIndex nodeIndex, GraphEditor::SlotIndex, GraphEditor::SlotIndex) override
            {
                if (nodeIndex < m_graph.states.size())
                {
                    m_selectedStateId = m_graph.states[nodeIndex].id;
                    m_selectedTransitionId = 0;
                    return;
                }

                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float factor = std::max(0.001f, m_viewState.mFactor);
                m_addStatePosition = ImVec2((mouse.x - m_canvasScreenPos.x) / factor - m_viewState.mPosition.x,
                                            (mouse.y - m_canvasScreenPos.y) / factor - m_viewState.mPosition.y);
                m_openAddStatePopup = true;
            }

            const size_t GetTemplateCount() override { return 1; }
            const GraphEditor::Template GetTemplate(GraphEditor::TemplateIndex) override { return m_template; }
            const size_t GetNodeCount() override { return m_graph.states.size(); }

            const GraphEditor::Node GetNode(GraphEditor::NodeIndex index) override
            {
                const auto &state = m_graph.states[index];
                const bool selected = std::find(m_selectedStateIds.begin(), m_selectedStateIds.end(), state.id) != m_selectedStateIds.end();
                return GraphEditor::Node{
                    "",
                    0,
                    ImRect(ImVec2(state.positionX, state.positionY),
                           ImVec2(state.positionX + kStateNodeWidth, state.positionY + kStateNodeHeight)),
                    selected,
                };
            }

            const size_t GetLinkCount() override { return m_graph.transitions.size(); }

            const GraphEditor::Link GetLink(GraphEditor::LinkIndex index) override
            {
                const auto &transition = m_graph.transitions[index];
                return GraphEditor::Link{
                    StateIndexForId(m_graph, transition.fromStateId),
                    0,
                    StateIndexForId(m_graph, transition.toStateId),
                    0,
                };
            }

        private:
            static const GraphEditor::Template m_template;

            assets::AnimationGraphAsset &m_graph;
            int &m_selectedStateId;
            std::vector<int> &m_selectedStateIds;
            int &m_selectedTransitionId;
            const GraphEditor::ViewState &m_viewState;
            ImVec2 m_canvasScreenPos;
            ImVec2 &m_addStatePosition;
            bool &m_openAddStatePopup;
            bool &m_dirty;
        };

        const GraphEditor::Template AnimationGraphDelegate::m_template = BuildStateTemplate();

        void DrawConditionEditor(assets::AnimationGraphAsset &graph,
                                 assets::AnimationGraphTransition &transition,
                                 bool &dirty)
        {
            if (ImGui::Button("Add Condition") && !graph.parameters.empty())
            {
                transition.conditions.push_back(assets::AnimationGraphCondition{
                    .parameterName = graph.parameters.front().name,
                });
                dirty = true;
            }

            constexpr const char *conditionModeLabels[] = {"If", "If Not", "Greater", "Less", "Equals", "Not Equal"};
            int conditionToRemove = -1;
            for (int conditionIndex = 0; conditionIndex < static_cast<int>(transition.conditions.size()); ++conditionIndex)
            {
                auto &condition = transition.conditions[static_cast<std::size_t>(conditionIndex)];
                ImGui::PushID(conditionIndex);
                if (!graph.parameters.empty())
                {
                    int selectedParameter = 0;
                    for (int parameterIndex = 0; parameterIndex < static_cast<int>(graph.parameters.size()); ++parameterIndex)
                    {
                        if (graph.parameters[static_cast<std::size_t>(parameterIndex)].name == condition.parameterName)
                        {
                            selectedParameter = parameterIndex;
                            break;
                        }
                    }
                    if (ImGui::BeginCombo("Parameter", graph.parameters[static_cast<std::size_t>(selectedParameter)].name.c_str()))
                    {
                        for (int parameterIndex = 0; parameterIndex < static_cast<int>(graph.parameters.size()); ++parameterIndex)
                        {
                            const bool selected = selectedParameter == parameterIndex;
                            if (ImGui::Selectable(graph.parameters[static_cast<std::size_t>(parameterIndex)].name.c_str(), selected))
                            {
                                condition.parameterName = graph.parameters[static_cast<std::size_t>(parameterIndex)].name;
                                dirty = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                int mode = static_cast<int>(condition.mode);
                if (ImGui::Combo("Mode", &mode, conditionModeLabels, IM_ARRAYSIZE(conditionModeLabels)))
                {
                    condition.mode = static_cast<assets::AnimationGraphConditionMode>(mode);
                    dirty = true;
                }
                if (ImGui::DragFloat("Threshold", &condition.threshold, 0.01f))
                {
                    dirty = true;
                }
                if (ImGui::Button("Remove Condition"))
                {
                    conditionToRemove = conditionIndex;
                }
                ImGui::Separator();
                ImGui::PopID();
            }

            if (conditionToRemove >= 0)
            {
                transition.conditions.erase(transition.conditions.begin() + conditionToRemove);
                dirty = true;
            }
        }
    }

    void AnimationGraphEditorPanel::LoadActiveGraph()
    {
        auto &editorShell = EditorShell::GetInstance();
        m_loadedReference = editorShell.GetActiveAnimationGraphAssetReference();
        bool loaded = false;
        m_graph = core::Engine::GetInstance().GetAssetManager().LoadAnimationGraphAsset(m_loadedReference, &loaded);
        if (!loaded)
        {
            m_graph = assets::CreateDefaultAnimationGraphAsset();
        }
        m_selectedStateId = m_graph.states.empty() ? 0 : m_graph.states.front().id;
        m_selectedStateIds.clear();
        if (m_selectedStateId != 0)
        {
            m_selectedStateIds.push_back(m_selectedStateId);
        }
        m_selectedTransitionId = 0;
        m_graphFit = GraphEditor::Fit_AllNodes;
        m_openAddStatePopup = false;
        m_dirty = false;
    }

    void AnimationGraphEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveAnimationGraphAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No animation graph selected.");
            return;
        }

        if (reference != m_loadedReference)
        {
            LoadActiveGraph();
        }

        m_graphOptions.mMinimap = ImRect(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f));
        m_graphOptions.mNodeSlotRadius = 6.0f;
        m_graphOptions.mLineThickness = 3.0f;
        m_graphOptions.mBorderThickness = 2.0f;
        m_graphOptions.mBorderSelectionThickness = 3.0f;
        m_graphOptions.mDrawIONameOnHover = false;
        m_graphOptions.mDisplayLinksAsCurves = true;

        ImGui::TextWrapped("Animation Graph: %s", reference.c_str());
        if (ImGui::Button("Fit"))
        {
            m_graphFit = GraphEditor::Fit_AllNodes;
        }

        ImGui::Separator();
        ImGui::Columns(2, "AnimationGraphEditorColumns", true);

        const ImVec2 graphSize(ImGui::GetContentRegionAvail().x, std::max(360.0f, ImGui::GetContentRegionAvail().y - 62.0f));
        ImGui::BeginChild("AnimationGraphCanvasHost", graphSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 canvasScreenPos = ImGui::GetCursorScreenPos();
        AnimationGraphDelegate delegate(m_graph,
                                        m_selectedStateId,
                                        m_selectedStateIds,
                                        m_selectedTransitionId,
                                        m_graphViewState,
                                        canvasScreenPos,
                                        m_addStatePosition,
                                        m_openAddStatePopup,
                                        m_dirty);
        GraphEditor::Show(delegate, m_graphOptions, m_graphViewState, true, &m_graphFit);
        if (m_openAddStatePopup)
        {
            ImGui::OpenPopup("AnimationGraphAddStatePopup");
            m_openAddStatePopup = false;
        }
        if (ImGui::BeginPopup("AnimationGraphAddStatePopup"))
        {
            if (ImGui::MenuItem("State"))
            {
                const int id = NextStateId(m_graph);
                m_graph.states.push_back(assets::AnimationGraphState{
                    .id = id,
                    .name = "State " + std::to_string(id),
                    .clipIndex = 0,
                    .positionX = m_addStatePosition.x,
                    .positionY = m_addStatePosition.y,
                });
                if (m_graph.defaultStateId == 0)
                {
                    m_graph.defaultStateId = id;
                }
                m_selectedStateId = id;
                m_selectedStateIds = {id};
                m_dirty = true;
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        ImGui::NextColumn();
        ImGui::TextUnformatted("Inspector");

        if (auto *state = FindState(m_graph, m_selectedStateId))
        {
            char nameBuffer[128]{};
            strncpy_s(nameBuffer, state->name.c_str(), _TRUNCATE);
            if (ImGui::InputText("State Name", nameBuffer, sizeof(nameBuffer)))
            {
                state->name = nameBuffer;
                m_dirty = true;
            }

            const auto clipOptions = CollectClipAssetOptions(editorShell.GetProject());
            std::string clipPreview = state->clipReference.empty() ? "None" : state->clipReference;
            for (const auto &option : clipOptions)
            {
                if (option.reference == state->clipReference)
                {
                    clipPreview = option.displayName;
                    break;
                }
            }

            if (ImGui::BeginCombo("Clip Asset", clipPreview.c_str()))
            {
                if (ImGui::Selectable("None", state->clipReference.empty()))
                {
                    state->clipReference.clear();
                    m_dirty = true;
                }
                for (const auto &option : clipOptions)
                {
                    const bool selected = option.reference == state->clipReference;
                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                    {
                        state->clipReference = option.reference;
                        if (state->clipName.empty())
                        {
                            state->clipName = ClipNameFromReference(option.reference);
                        }
                        m_dirty = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(state->clipReference.empty());
            if (ImGui::Button("Clear##ClipAsset"))
            {
                state->clipReference.clear();
                m_dirty = true;
            }
            ImGui::EndDisabled();

            char clipBuffer[128]{};
            strncpy_s(clipBuffer, state->clipName.c_str(), _TRUNCATE);
            if (ImGui::InputText("Clip Name", clipBuffer, sizeof(clipBuffer)))
            {
                state->clipName = clipBuffer;
                m_dirty = true;
            }
            if (ImGui::DragInt("Clip Index", &state->clipIndex, 0.1f, 0, 999))
            {
                m_dirty = true;
            }
            if (ImGui::DragFloat("Speed", &state->speed, 0.01f, 0.0f, 10.0f))
            {
                m_dirty = true;
            }
            if (ImGui::Checkbox("Loop", &state->loop))
            {
                m_dirty = true;
            }
            if (ImGui::Button("Make Default"))
            {
                m_graph.defaultStateId = state->id;
                m_dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete State"))
            {
                const int removedId = state->id;
                RemoveState(m_graph, removedId);
                m_selectedStateIds.clear();
                m_selectedStateId = m_graph.states.empty() ? 0 : m_graph.states.front().id;
                if (m_selectedStateId != 0)
                {
                    m_selectedStateIds.push_back(m_selectedStateId);
                }
                m_dirty = true;
            }

            ImGui::SeparatorText("Outgoing Transitions");
            for (auto &transition : m_graph.transitions)
            {
                if (transition.fromStateId != m_selectedStateId)
                {
                    continue;
                }
                auto *destination = FindState(m_graph, transition.toStateId);
                const std::string label = "To " + std::string(destination ? destination->name : "Missing") + "##" + std::to_string(transition.id);
                if (ImGui::TreeNode(label.c_str()))
                {
                    m_selectedTransitionId = transition.id;
                    if (ImGui::DragFloat("Blend Duration", &transition.duration, 0.01f, 0.0f, 10.0f))
                    {
                        m_dirty = true;
                    }
                    if (ImGui::Checkbox("Has Exit Time", &transition.hasExitTime))
                    {
                        m_dirty = true;
                    }
                    if (ImGui::DragFloat("Exit Time", &transition.exitTime, 0.01f, 0.0f, 10.0f))
                    {
                        m_dirty = true;
                    }
                    DrawConditionEditor(m_graph, transition, m_dirty);
                    if (ImGui::Button("Delete Transition"))
                    {
                        const int removedId = transition.id;
                        m_graph.transitions.erase(std::remove_if(m_graph.transitions.begin(), m_graph.transitions.end(),
                                                                 [removedId](const assets::AnimationGraphTransition &candidate)
                                                                 {
                                                                     return candidate.id == removedId;
                                                                 }),
                                                  m_graph.transitions.end());
                        m_selectedTransitionId = 0;
                        m_dirty = true;
                        ImGui::TreePop();
                        break;
                    }
                    ImGui::TreePop();
                }
            }
        }
        else
        {
            ImGui::TextDisabled("Select a state node.");
        }

        ImGui::SeparatorText("Parameters");
        if (ImGui::Button("Add Float"))
        {
            m_graph.parameters.push_back(assets::AnimationGraphParameter{
                .id = NextParameterId(m_graph),
                .name = "Speed",
                .type = assets::AnimationGraphParameterType::Float,
            });
            m_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Bool"))
        {
            m_graph.parameters.push_back(assets::AnimationGraphParameter{
                .id = NextParameterId(m_graph),
                .name = "IsWalking",
                .type = assets::AnimationGraphParameterType::Bool,
            });
            m_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Trigger"))
        {
            m_graph.parameters.push_back(assets::AnimationGraphParameter{
                .id = NextParameterId(m_graph),
                .name = "Jump",
                .type = assets::AnimationGraphParameterType::Trigger,
            });
            m_dirty = true;
        }

        constexpr const char *parameterTypeLabels[] = {"Float", "Int", "Bool", "Trigger"};
        int parameterToRemove = -1;
        for (int parameterIndex = 0; parameterIndex < static_cast<int>(m_graph.parameters.size()); ++parameterIndex)
        {
            auto &parameter = m_graph.parameters[static_cast<std::size_t>(parameterIndex)];
            ImGui::PushID(parameterIndex);
            char nameBuffer[128]{};
            strncpy_s(nameBuffer, parameter.name.c_str(), _TRUNCATE);
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
            {
                parameter.name = nameBuffer;
                m_dirty = true;
            }
            int type = static_cast<int>(parameter.type);
            if (ImGui::Combo("Type", &type, parameterTypeLabels, IM_ARRAYSIZE(parameterTypeLabels)))
            {
                parameter.type = static_cast<assets::AnimationGraphParameterType>(type);
                m_dirty = true;
            }
            if (parameter.type == assets::AnimationGraphParameterType::Float)
            {
                m_dirty |= ImGui::DragFloat("Value", &parameter.floatValue, 0.01f);
            }
            else if (parameter.type == assets::AnimationGraphParameterType::Int)
            {
                m_dirty |= ImGui::DragInt("Value", &parameter.intValue);
            }
            else
            {
                m_dirty |= ImGui::Checkbox("Value", &parameter.boolValue);
            }
            if (ImGui::Button("Remove"))
            {
                parameterToRemove = parameterIndex;
            }
            ImGui::TextDisabled("%s", ParameterTypeLabel(parameter.type));
            ImGui::Separator();
            ImGui::PopID();
        }
        if (parameterToRemove >= 0)
        {
            const std::string removedName = m_graph.parameters[static_cast<std::size_t>(parameterToRemove)].name;
            m_graph.parameters.erase(m_graph.parameters.begin() + parameterToRemove);
            for (auto &transition : m_graph.transitions)
            {
                transition.conditions.erase(std::remove_if(transition.conditions.begin(), transition.conditions.end(),
                                                           [&removedName](const assets::AnimationGraphCondition &condition)
                                                           {
                                                               return condition.parameterName == removedName;
                                                           }),
                                            transition.conditions.end());
            }
            m_dirty = true;
        }

        if (m_selectedTransitionId != 0)
        {
            if (auto *transition = FindTransition(m_graph, m_selectedTransitionId))
            {
                ImGui::SeparatorText("Selected Transition");
                ImGui::TextDisabled("%s", ConditionModeLabel(transition->conditions.empty() ? assets::AnimationGraphConditionMode::If : transition->conditions.front().mode));
            }
        }

        ImGui::Columns(1);

        ImGui::Separator();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Save"))
        {
            std::string errorMessage;
            if (core::Engine::GetInstance().GetAssetManager().SaveAnimationGraphAsset(reference, m_graph, &errorMessage))
            {
                m_dirty = false;
                editorShell.MarkProjectDirty();
                editorShell.MarkSceneDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved animation graph: " + reference);
            }
            else
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save animation graph." : errorMessage);
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
