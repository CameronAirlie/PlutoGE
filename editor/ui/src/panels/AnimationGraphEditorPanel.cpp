#include "PlutoGE/ui/panels/AnimationGraphEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
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

        std::vector<ClipAssetOption> CollectAnimationGraphAssetOptions(const assets::Project *project,
                                                                        std::string_view excludedReference)
        {
            std::vector<ClipAssetOption> options;
            if (!project)
                return options;
            for (const auto &asset : project->GetManifest().assetEntries)
            {
                if (asset.type != assets::ProjectAssetType::AnimationGraph || asset.reference == excludedReference)
                    continue;
                std::string displayName = asset.reference;
                if (StartsWith(displayName, assets::Project::kProjectAssetScheme))
                    displayName.erase(0, assets::Project::kProjectAssetScheme.size());
                options.push_back({.reference = asset.reference, .displayName = std::move(displayName)});
            }
            std::sort(options.begin(), options.end(),
                      [](const ClipAssetOption &left, const ClipAssetOption &right)
                      { return left.displayName < right.displayName; });
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

        int NextBoneMaskId(const assets::AnimationGraphAsset &graph)
        {
            int id = 1;
            for (const auto &mask : graph.boneMasks)
                id = std::max(id, mask.id + 1);
            return id;
        }

        int NextLayerId(const assets::AnimationGraphAsset &graph)
        {
            int id = 1;
            for (const auto &layer : graph.layers)
                id = std::max(id, layer.id + 1);
            return id;
        }

        assets::AnimationGraphBoneMask *FindBoneMask(assets::AnimationGraphAsset &graph, int id)
        {
            const auto it = std::find_if(graph.boneMasks.begin(), graph.boneMasks.end(),
                                         [id](const assets::AnimationGraphBoneMask &mask) { return mask.id == id; });
            return it == graph.boneMasks.end() ? nullptr : &*it;
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

        void RefreshAnimationGraphComponents(scene::Entity &entity, const std::string &animationGraphReference)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            const std::string resolvedAnimationGraphPath = assetManager.ResolveAssetPath(animationGraphReference);
            if (auto *animationComponent = entity.GetComponent<scene::AnimationComponent>();
                animationComponent && !animationComponent->GetAnimationGraphAssetReference().empty())
            {
                const std::string componentGraphReference = animationComponent->GetAnimationGraphAssetReference();
                const std::string resolvedComponentGraphPath = assetManager.ResolveAssetPath(componentGraphReference);
                if (componentGraphReference == animationGraphReference ||
                    (!resolvedAnimationGraphPath.empty() && resolvedComponentGraphPath == resolvedAnimationGraphPath))
                {
                    animationComponent->SetAnimationGraphAssetReference(componentGraphReference);
                }
            }

            for (auto *child : entity.GetChildren())
            {
                if (child)
                {
                    RefreshAnimationGraphComponents(*child, animationGraphReference);
                }
            }
        }

        void RefreshSceneAnimationGraphComponents(scene::Scene &scene, const std::string &animationGraphReference)
        {
            for (auto *rootEntity : scene.GetRootEntities())
            {
                if (rootEntity)
                {
                    RefreshAnimationGraphComponents(*rootEntity, animationGraphReference);
                }
            }
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
                // GraphEditor treats input slots as single-link and calls DelLink before
                // adding a new link. Animation states allow multiple incoming transitions,
                // so deletion is handled explicitly from the transition inspector instead.
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
                        state->clipName = ClipNameFromReference(option.reference);
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
                const std::string oldName = parameter.name;
                parameter.name = nameBuffer;
                for (auto &transition : m_graph.transitions)
                    for (auto &condition : transition.conditions)
                        if (condition.parameterName == oldName)
                            condition.parameterName = parameter.name;
                for (auto &layer : m_graph.layers)
                {
                    if (layer.activationParameter == oldName)
                        layer.activationParameter = parameter.name;
                    if (layer.weightParameter == oldName)
                        layer.weightParameter = parameter.name;
                }
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
            for (auto &layer : m_graph.layers)
            {
                if (layer.activationParameter == removedName)
                    layer.activationParameter.clear();
                if (layer.weightParameter == removedName)
                    layer.weightParameter.clear();
            }
            m_dirty = true;
        }


        ImGui::SeparatorText("Layered Animation");
        ImGui::TextWrapped("Stack reusable animation graphs or clips, then restrict each result with a bone mask. Later layers can partially or completely override earlier layers.");
        const auto layerClipOptions = CollectClipAssetOptions(editorShell.GetProject());
        const auto layerGraphOptions = CollectAnimationGraphAssetOptions(editorShell.GetProject(), reference);
        if (ImGui::Button("Add Graph Layer"))
        {
            m_graph.layers.push_back(assets::AnimationGraphLayer{
                .id = NextLayerId(m_graph),
                .name = "Graph Layer " + std::to_string(m_graph.layers.size() + 1),
                .graphReference = layerGraphOptions.empty() ? std::string{} : layerGraphOptions.front().reference,
            });
            m_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Upper Body Action Preset"))
        {
            const int maskId = NextBoneMaskId(m_graph);
            assets::AnimationGraphBoneMask mask{
                .id = maskId,
                .name = "Upper Body",
                .defaultWeight = 0.0f,
            };
            mask.entries = {
                {.bone = render::HumanoidBone::Spine, .weight = 0.25f, .includeChildren = true},
                {.bone = render::HumanoidBone::Chest, .weight = 0.75f, .includeChildren = true},
                {.bone = render::HumanoidBone::UpperChest, .weight = 1.0f, .includeChildren = true},
                {.bone = render::HumanoidBone::LeftShoulder, .weight = 1.0f, .includeChildren = true},
                {.bone = render::HumanoidBone::RightShoulder, .weight = 1.0f, .includeChildren = true},
            };
            m_graph.boneMasks.push_back(std::move(mask));

            const bool hasShootParameter = std::any_of(m_graph.parameters.begin(), m_graph.parameters.end(),
                                                        [](const assets::AnimationGraphParameter &parameter) { return parameter.name == "Shoot"; });
            if (!hasShootParameter)
            {
                m_graph.parameters.push_back(assets::AnimationGraphParameter{
                    .id = NextParameterId(m_graph),
                    .name = "Shoot",
                    .type = assets::AnimationGraphParameterType::Trigger,
                });
            }
            m_graph.layers.push_back(assets::AnimationGraphLayer{
                .id = NextLayerId(m_graph),
                .name = "Upper Body Action",
                .maskId = maskId,
                .activationParameter = "Shoot",
            });
            m_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Empty Layer"))
        {
            m_graph.layers.push_back(assets::AnimationGraphLayer{
                .id = NextLayerId(m_graph),
                .name = "Layer " + std::to_string(m_graph.layers.size() + 1),
            });
            m_dirty = true;
        }

        int layerToRemove = -1;
        for (int layerIndex = 0; layerIndex < static_cast<int>(m_graph.layers.size()); ++layerIndex)
        {
            auto &layer = m_graph.layers[static_cast<size_t>(layerIndex)];
            ImGui::PushID(10000 + layerIndex);
            const std::string title = layer.name.empty() ? "Unnamed Layer" : layer.name;
            if (ImGui::TreeNode(title.c_str()))
            {
                char nameBuffer[128]{};
                strncpy_s(nameBuffer, layer.name.c_str(), _TRUNCATE);
                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                {
                    layer.name = nameBuffer;
                    m_dirty = true;
                }

                std::string graphPreview = layer.graphReference.empty() ? "None (use clip)" : layer.graphReference;
                for (const auto &option : layerGraphOptions)
                    if (option.reference == layer.graphReference)
                        graphPreview = option.displayName;
                if (ImGui::BeginCombo("Animation Graph", graphPreview.c_str()))
                {
                    if (ImGui::Selectable("None (use clip)", layer.graphReference.empty()))
                    {
                        layer.graphReference.clear();
                        m_dirty = true;
                    }
                    for (const auto &option : layerGraphOptions)
                    {
                        const bool selected = option.reference == layer.graphReference;
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            layer.graphReference = option.reference;
                            m_dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                if (layer.graphReference.empty())
                {
                    std::string clipPreview = layer.clipReference.empty() ? "None" : layer.clipReference;
                    for (const auto &option : layerClipOptions)
                        if (option.reference == layer.clipReference)
                            clipPreview = option.displayName;
                    if (ImGui::BeginCombo("Clip Asset", clipPreview.c_str()))
                    {
                        if (ImGui::Selectable("None", layer.clipReference.empty()))
                        {
                            layer.clipReference.clear();
                            m_dirty = true;
                        }
                        for (const auto &option : layerClipOptions)
                        {
                            const bool selected = option.reference == layer.clipReference;
                            if (ImGui::Selectable(option.displayName.c_str(), selected))
                            {
                                layer.clipReference = option.reference;
                                layer.clipName = ClipNameFromReference(option.reference);
                                m_dirty = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    char clipNameBuffer[128]{};
                    strncpy_s(clipNameBuffer, layer.clipName.c_str(), _TRUNCATE);
                    if (ImGui::InputText("Clip Name", clipNameBuffer, sizeof(clipNameBuffer)))
                    {
                        layer.clipName = clipNameBuffer;
                        m_dirty = true;
                    }
                    if (ImGui::DragInt("Clip Index", &layer.clipIndex, 0.1f, 0, 999))
                        m_dirty = true;
                }

                const auto *selectedMask = FindBoneMask(m_graph, layer.maskId);
                const char *maskPreview = selectedMask ? selectedMask->name.c_str() : "Full Body";
                if (ImGui::BeginCombo("Bone Mask", maskPreview))
                {
                    if (ImGui::Selectable("Full Body", layer.maskId == 0))
                    {
                        layer.maskId = 0;
                        m_dirty = true;
                    }
                    for (const auto &mask : m_graph.boneMasks)
                    {
                        if (ImGui::Selectable(mask.name.c_str(), layer.maskId == mask.id))
                        {
                            layer.maskId = mask.id;
                            m_dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                int blendMode = static_cast<int>(layer.blendMode);
                constexpr const char *blendModes[] = {"Override", "Additive"};
                if (ImGui::Combo("Blend Mode", &blendMode, blendModes, IM_ARRAYSIZE(blendModes)))
                {
                    layer.blendMode = static_cast<assets::AnimationGraphLayerBlendMode>(blendMode);
                    m_dirty = true;
                }
                m_dirty |= ImGui::SliderFloat("Weight", &layer.weight, 0.0f, 1.0f);
                m_dirty |= ImGui::DragFloat("Speed", &layer.speed, 0.01f, 0.0f, 10.0f);
                m_dirty |= ImGui::DragFloat("Fade In", &layer.fadeIn, 0.01f, 0.0f, 5.0f);
                m_dirty |= ImGui::DragFloat("Fade Out", &layer.fadeOut, 0.01f, 0.0f, 5.0f);
                if (layer.graphReference.empty())
                    m_dirty |= ImGui::Checkbox("Loop", &layer.loop);
                m_dirty |= ImGui::Checkbox("Restart on Activation", &layer.restartOnActivation);
                m_dirty |= ImGui::Checkbox("Enabled", &layer.enabled);

                const char *activationPreview = layer.activationParameter.empty() ? "Always" : layer.activationParameter.c_str();
                if (ImGui::BeginCombo("Activation", activationPreview))
                {
                    if (ImGui::Selectable("Always", layer.activationParameter.empty()))
                    {
                        layer.activationParameter.clear();
                        m_dirty = true;
                    }
                    for (const auto &parameter : m_graph.parameters)
                    {
                        if (parameter.type != assets::AnimationGraphParameterType::Bool &&
                            parameter.type != assets::AnimationGraphParameterType::Trigger)
                            continue;
                        if (ImGui::Selectable(parameter.name.c_str(), layer.activationParameter == parameter.name))
                        {
                            layer.activationParameter = parameter.name;
                            m_dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                const char *weightPreview = layer.weightParameter.empty() ? "None" : layer.weightParameter.c_str();
                if (ImGui::BeginCombo("Weight Parameter", weightPreview))
                {
                    if (ImGui::Selectable("None", layer.weightParameter.empty()))
                    {
                        layer.weightParameter.clear();
                        m_dirty = true;
                    }
                    for (const auto &parameter : m_graph.parameters)
                    {
                        if (parameter.type != assets::AnimationGraphParameterType::Float)
                            continue;
                        if (ImGui::Selectable(parameter.name.c_str(), layer.weightParameter == parameter.name))
                        {
                            layer.weightParameter = parameter.name;
                            m_dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Delete Layer"))
                    layerToRemove = layerIndex;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (layerToRemove >= 0)
        {
            m_graph.layers.erase(m_graph.layers.begin() + layerToRemove);
            m_dirty = true;
        }

        ImGui::SeparatorText("Bone Masks");
        if (ImGui::Button("Add Bone Mask"))
        {
            m_graph.boneMasks.push_back(assets::AnimationGraphBoneMask{
                .id = NextBoneMaskId(m_graph),
                .name = "Bone Mask " + std::to_string(m_graph.boneMasks.size() + 1),
            });
            m_dirty = true;
        }
        int maskToRemove = -1;
        for (int maskIndex = 0; maskIndex < static_cast<int>(m_graph.boneMasks.size()); ++maskIndex)
        {
            auto &mask = m_graph.boneMasks[static_cast<size_t>(maskIndex)];
            ImGui::PushID(20000 + maskIndex);
            if (ImGui::TreeNode(mask.name.c_str()))
            {
                char nameBuffer[128]{};
                strncpy_s(nameBuffer, mask.name.c_str(), _TRUNCATE);
                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                {
                    mask.name = nameBuffer;
                    m_dirty = true;
                }
                m_dirty |= ImGui::SliderFloat("Default Weight", &mask.defaultWeight, 0.0f, 1.0f);
                if (ImGui::Button("Add Bone"))
                {
                    mask.entries.push_back({});
                    m_dirty = true;
                }
                int entryToRemove = -1;
                for (int entryIndex = 0; entryIndex < static_cast<int>(mask.entries.size()); ++entryIndex)
                {
                    auto &entry = mask.entries[static_cast<size_t>(entryIndex)];
                    ImGui::PushID(entryIndex);
                    int bone = static_cast<int>(entry.bone);
                    if (ImGui::Combo("Bone", &bone, render::kHumanoidBoneNames.data(), static_cast<int>(render::kHumanoidBoneCount)))
                    {
                        entry.bone = static_cast<render::HumanoidBone>(bone);
                        m_dirty = true;
                    }
                    m_dirty |= ImGui::SliderFloat("Bone Weight", &entry.weight, 0.0f, 1.0f);
                    m_dirty |= ImGui::Checkbox("Include Children", &entry.includeChildren);
                    ImGui::SameLine();
                    if (ImGui::Button("Remove Bone"))
                        entryToRemove = entryIndex;
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (entryToRemove >= 0)
                {
                    mask.entries.erase(mask.entries.begin() + entryToRemove);
                    m_dirty = true;
                }
                if (ImGui::Button("Delete Mask"))
                    maskToRemove = maskIndex;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (maskToRemove >= 0)
        {
            const int removedId = m_graph.boneMasks[static_cast<size_t>(maskToRemove)].id;
            m_graph.boneMasks.erase(m_graph.boneMasks.begin() + maskToRemove);
            for (auto &layer : m_graph.layers)
                if (layer.maskId == removedId)
                    layer.maskId = 0;
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
                if (auto *scene = editorShell.GetScene())
                {
                    RefreshSceneAnimationGraphComponents(*scene, reference);
                }
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
