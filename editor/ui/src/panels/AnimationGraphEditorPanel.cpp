#include "PlutoGE/ui/panels/AnimationGraphEditorPanel.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/animgraph/AnimationGraphInspector.h"
#include "PlutoGE/ui/animgraph/AnimationGraphSidebar.h"
#include "PlutoGE/ui/animgraph/StateMachineView.h"

#include <misc/cpp/imgui_stdlib.h>

#include <imgui_internal.h>

namespace PlutoGE::ui
{
    namespace ag = assets::animgraph;

    namespace
    {
        constexpr const char *kDot = "\xC2\xB7"; // middle dot

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

        std::string AssetDisplayName(const std::string &reference)
        {
            std::string name = reference;
            if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos)
                name = name.substr(slash + 1);
            if (const auto dot = name.find_last_of('.'); dot != std::string::npos)
                name.erase(dot);
            return name;
        }
    }

    bool AnimationGraphEditorPanel::OwnsKeyboardFocus() const
    {
        if (!IsOpen() || !ImGui::GetCurrentContext())
            return false;
        auto *window = ImGui::FindWindowByName(GetName().c_str());
        auto *focused = ImGui::GetCurrentContext()->NavWindow;
        return window && focused && ImGui::IsWindowChildOf(focused, window, true, false);
    }

    void AnimationGraphEditorPanel::LoadActiveGraph()
    {
        m_loadedReference = EditorShell::GetInstance().GetActiveAnimationGraphAssetReference();
        bool loaded = false;
        auto graph = core::Engine::GetInstance().GetAssetManager().LoadAnimationGraphAsset(m_loadedReference, &loaded);
        if (!loaded)
            graph = assets::CreateDefaultAnimationGraphAsset();
        m_document.Load(m_loadedReference, std::move(graph));
        m_canvas = {};
        m_canvasSearch.clear();
    }

    bool AnimationGraphEditorPanel::Save()
    {
        auto &editorShell = EditorShell::GetInstance();
        m_document.CommitPendingEdits();
        std::string errorMessage;
        if (!core::Engine::GetInstance().GetAssetManager().SaveAnimationGraphAsset(m_loadedReference, m_document.Graph(), &errorMessage))
        {
            editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save animation graph." : errorMessage);
            return false;
        }
        if (auto *scene = editorShell.GetScene())
            RefreshSceneAnimationGraphComponents(*scene, m_loadedReference);
        m_document.MarkSaved();
        editorShell.MarkProjectDirty();
        editorShell.MarkSceneDirty();
        editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved animation graph: " + m_loadedReference);
        return true;
    }

    void AnimationGraphEditorPanel::HandleShortcuts()
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (!m_focused || io.WantTextInput || !(io.KeyCtrl || io.KeySuper) ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
            m_document.Undo();
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (ImGui::IsKeyPressed(ImGuiKey_Z, false) && io.KeyShift))
            m_document.Redo();
        else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && m_document.IsDirty())
            Save();
    }

    void AnimationGraphEditorPanel::DrawToolbar()
    {
        const auto &graph = m_document.Graph();
        const bool readOnly = assets::Project::IsEngineAssetReference(m_loadedReference);

        // Asset name, then breadcrumbs through the group hierarchy.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(AssetDisplayName(m_loadedReference).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", m_loadedReference.c_str());
        if (m_document.IsDirty())
        {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(GetEditorColor(EditorColor::Warning), "*");
        }
        ImGui::SameLine(0.0f, 16.0f);

        const auto path = ag::GroupPath(graph, m_document.CurrentGroupId());
        for (std::size_t index = 0; index < path.size(); ++index)
        {
            const int groupId = path[index];
            const std::string name = groupId == assets::kAnimationGraphRootGroupId
                                         ? std::string("Root")
                                         : ag::NodeName(graph, assets::AnimationGraphNodeRef::ForGroup(groupId));
            ImGui::PushID(groupId);
            if (index + 1 == path.size())
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(name.c_str());
            }
            else
            {
                if (ImGui::SmallButton(name.c_str()))
                    m_document.EnterGroup(groupId);
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled(">");
                ImGui::SameLine(0.0f, 4.0f);
            }
            ImGui::PopID();
        }

        // Right-aligned tools.
        const float buttonWidth = ImGui::GetFrameHeight();
        const float searchWidth = 180.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float toolsWidth = searchWidth + buttonWidth * 3.0f + ImGui::CalcTextSize("Live").x + buttonWidth +
                                 ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Revert").x +
                                 ImGui::GetStyle().FramePadding.x * 4.0f + spacing * 8.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + spacing, ImGui::GetWindowContentRegionMax().x - toolsWidth));

        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##CanvasSearch", "Highlight...", &m_canvasSearch);
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.CanUndo());
        if (widgets::IconButton("Undo", icons::Revert, "Undo (Ctrl+Z)"))
            m_document.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.CanRedo());
        if (widgets::IconButton("Redo", icons::Rotate, "Redo (Ctrl+Y)"))
            m_document.Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (widgets::IconButton("Frame", icons::Frame, "Frame all (F frames the selection)"))
            m_canvas.RequestFrameAll();
        ImGui::SameLine();
        ImGui::Checkbox("Live", &m_liveEnabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show the selected entity's playback on the canvas.");
        ImGui::SameLine();
        ImGui::BeginDisabled(readOnly || !m_document.IsDirty());
        if (widgets::PrimaryButton("Save"))
            Save();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_document.IsDirty());
        if (ImGui::Button("Revert"))
            LoadActiveGraph();
        ImGui::EndDisabled();
    }

    void AnimationGraphEditorPanel::DrawStatusBar()
    {
        const auto &graph = m_document.Graph();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu states %s %zu groups %s %zu transitions %s %zu parameters %s zoom %d%%",
                            graph.states.size(), kDot, graph.groups.size(), kDot, graph.transitions.size(), kDot,
                            graph.parameters.size(), kDot, static_cast<int>(m_canvas.Zoom() * 100.0f + 0.5f));
        if (assets::Project::IsEngineAssetReference(m_loadedReference))
        {
            ImGui::SameLine();
            ImGui::TextColored(GetEditorColor(EditorColor::Warning), "Engine asset (read-only)");
        }
        if (const auto &overlay = m_live.Overlay())
        {
            ImGui::SameLine(0.0f, 24.0f);
            const auto *active = ag::FindState(graph, overlay->activeStateId);
            ImGui::TextColored(GetEditorColor(EditorColor::Success), "Live: %s in %s", m_live.EntityName().c_str(),
                               active ? active->name.c_str() : "(state not in this graph)");
            if (overlay->transitionToStateId != 0)
            {
                const auto *destination = ag::FindState(graph, overlay->transitionToStateId);
                ImGui::SameLine();
                ImGui::TextDisabled("blending to %s (%d%%)", destination ? destination->name.c_str() : "?",
                                    static_cast<int>(overlay->transitionProgress * 100.0f));
            }
        }
    }

    void AnimationGraphEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveAnimationGraphAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No animation graph selected. Open one from the Content Browser.");
            return;
        }
        if (reference != m_loadedReference)
            LoadActiveGraph();

        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (m_liveEnabled)
            m_live.Refresh(m_loadedReference, m_document.Graph());
        else
            m_live = {};

        animgraph::EditorContext context{m_document, m_canvas, m_live, editorShell.GetProject()};
        HandleShortcuts();
        DrawToolbar();
        ImGui::Separator();

        const float footerHeight = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("AnimationGraphEditorBody", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::BeginTable("AnimationGraphEditorLayout", 3,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Sidebar", ImGuiTableColumnFlags_WidthFixed, 270.0f);
            ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 380.0f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild("Sidebar", ImVec2(0.0f, 0.0f));
            animgraph::DrawAnimationGraphSidebar(context);
            ImGui::EndChild();

            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("Canvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
            const auto view = animgraph::BuildStateMachineView(m_document.Graph(), m_document.CurrentGroupId());
            animgraph::CanvasOverlay overlay;
            overlay.live = m_live.Overlay();
            overlay.search = m_canvasSearch;
            m_canvas.Draw(m_document, view, overlay);
            ImGui::EndChild();

            ImGui::TableSetColumnIndex(2);
            ImGui::BeginChild("Inspector", ImVec2(0.0f, 0.0f));
            animgraph::DrawAnimationGraphInspector(context);
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::EndChild();

        DrawStatusBar();

        // A drag or text edit becomes one undo step once it ends.
        if (!ImGui::IsAnyItemActive() && !m_canvas.IsInteracting())
            m_document.CommitPendingEdits();
    }
}
