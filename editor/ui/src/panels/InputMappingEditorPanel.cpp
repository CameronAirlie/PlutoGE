#include "PlutoGE/ui/panels/InputMappingEditorPanel.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/input/InputMappingSession.h"
#include "PlutoGE/ui/input/InputMappingWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        const platform::InputState &EditorInput()
        {
            return core::Engine::GetInstance().GetWindow().GetInputState();
        }
    }

    InputMappingEditorPanel::InputMappingEditorPanel(const PanelConfig &config) : Panel(config) {}
    InputMappingEditorPanel::~InputMappingEditorPanel() = default;

    void InputMappingEditorPanel::OnProjectChanged()
    {
        m_session.reset();
        m_reference.clear();
        m_followedReference.clear();
        m_pendingReference.reset();
        m_error.clear();
    }

    bool InputMappingEditorPanel::HasUnsavedChanges() const
    {
        return m_session && m_session->IsDirty();
    }

    void InputMappingEditorPanel::Open(std::unique_ptr<InputMappingSession> session)
    {
        SetOpen(true);
        m_session = std::move(session);
        m_reference.clear();
        m_pendingReference.reset();
        m_error.clear();
        m_analysis = {};
        m_actions = {};
        m_requestedTab = Tab::Actions;
    }

    void InputMappingEditorPanel::OpenReference(const std::string &reference)
    {
        m_error.clear();
        m_session = InputMappingSession::OpenAsset(reference, m_error);
        m_reference = reference;
        m_analysis = {};
        m_actions = {};
        m_requestedTab = Tab::Actions;
    }

    void InputMappingEditorPanel::FollowShell()
    {
        const auto &reference = EditorShell::GetInstance().GetActiveInputMappingAssetReference();
        if (reference == m_followedReference)
            return;
        m_followedReference = reference;
        if (reference.empty() || (reference == m_reference && m_session))
            return;
        if (HasUnsavedChanges())
        {
            m_pendingReference = reference;
            m_openSavePrompt = true;
            return;
        }
        OpenReference(reference);
    }

    void InputMappingEditorPanel::Render()
    {
        FollowShell();
        DrawSavePrompt();
        if (!m_session)
        {
            DrawEmptyState();
            return;
        }

        HandleShortcuts();
        m_analysis.Update(*m_session);
        DrawToolbar();
        DrawProblems();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##InputMappingTabs"))
        {
            const auto tab = [this](const char *label, Tab id)
            {
                const ImGuiTabItemFlags flags = m_requestedTab == id ? ImGuiTabItemFlags_SetSelected : 0;
                return ImGui::BeginTabItem(label, nullptr, flags);
            };
            if (tab("Actions", Tab::Actions))
            {
                input::DrawActionsView(*m_session, m_actions, m_analysis, &EditorInput());
                ImGui::EndTabItem();
            }
            const int problems = m_analysis.ProblemCount();
            const std::string overview = problems > 0 ? "Overview  " + std::string(icons::Warning) + " " + std::to_string(problems) + "###Overview"
                                                      : std::string("Overview###Overview");
            std::optional<int> show;
            if (tab(overview.c_str(), Tab::Overview))
            {
                show = input::DrawOverviewView(*m_session, m_analysis);
                ImGui::EndTabItem();
            }
            if (tab("Devices", Tab::Devices))
            {
                input::DrawDevicesView(&EditorInput());
                ImGui::EndTabItem();
            }
            m_requestedTab.reset();
            if (show)
            {
                m_actions.selected = *show;
                m_actions.expandedBinding = -1;
                m_requestedTab = Tab::Actions;
            }
            ImGui::EndTabBar();
        }
        m_session->EndFrame(ImGui::IsAnyItemActive());
    }

    void InputMappingEditorPanel::DrawEmptyState()
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        widgets::PageTitle("Input Mappings", "Name what the player can do, then choose the keys and buttons that do it.");
        if (!project)
        {
            ImGui::TextDisabled("Open a project to edit input mappings.");
            return;
        }
        if (!m_reference.empty() && !m_error.empty())
        {
            ImGui::TextColored(GetEditorColor(EditorColor::Error), "%s  %s", icons::Warning, m_reference.c_str());
            ImGui::TextWrapped("%s", m_error.c_str());
            if (ImGui::Button("Try Again"))
                OpenReference(m_reference);
            ImGui::Separator();
            ImGui::Spacing();
        }
        ImGui::TextWrapped("A mapping lists actions such as Jump or Fire, each with the inputs that trigger it on keyboard, mouse "
                           "and controller. Scripts ask for actions by name, so controls can change without touching code.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        if (RenderProjectAssetPicker("Open mapping", project, assets::ProjectAssetType::InputMapping, m_pickerReference) &&
            !m_pickerReference.empty())
            shell.OpenInputMappingAsset(m_pickerReference);
        ImGui::TextDisabled("To make a new one, use Create > Input Mapping in the Content Browser.");
    }

    void InputMappingEditorPanel::DrawToolbar()
    {
        auto &session = *m_session;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(icons::Gamepad);
        ImGui::SameLine();
        ImGui::TextUnformatted(session.GetLabel().c_str());
        if (!m_reference.empty())
            ImGui::SetItemTooltip("%s", m_reference.c_str());
        if (session.IsDirty())
        {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled("(unsaved)");
        }

        // Actions sit at the right edge.
        const float width = 5.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
        ImGui::BeginDisabled(!session.CanUndo());
        if (widgets::IconButton("##Undo", icons::ArrowLeft, "Undo (Ctrl+Z)"))
            session.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!session.CanRedo());
        if (widgets::IconButton("##Redo", icons::ArrowRight, "Redo (Ctrl+Y)"))
            session.Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!session.IsDirty());
        if (widgets::IconButton("##Save", icons::Save, "Save (Ctrl+S)"))
            Save();
        ImGui::SameLine();
        if (widgets::IconButton("##Revert", icons::Revert, "Discard unsaved changes"))
            session.Revert();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (widgets::IconButton("##Close", icons::Close, "Close this mapping"))
        {
            if (HasUnsavedChanges())
            {
                m_pendingReference = std::string{};
                m_openSavePrompt = true;
            }
            else
                m_session.reset();
        }
        if (!m_error.empty())
            ImGui::TextColored(GetEditorColor(EditorColor::Error), "%s", m_error.c_str());
    }

    void InputMappingEditorPanel::DrawProblems()
    {
        const int count = m_analysis.ProblemCount();
        if (count == 0 || !m_session)
            return;
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
        const bool open = ImGui::TreeNode("##Problems", "%s  %d problem%s", icons::Warning, count, count == 1 ? "" : "s");
        ImGui::PopStyleColor();
        if (!open)
            return;
        const auto &actions = m_session->GetActions();
        const auto select = [&](const std::string &name)
        {
            for (int index = 0; index < static_cast<int>(actions.size()); ++index)
                if (actions[index].name == name)
                {
                    m_actions.selected = index;
                    m_actions.expandedBinding = -1;
                    m_requestedTab = Tab::Actions;
                    return;
                }
        };
        for (const auto &name : m_analysis.invalidNames)
        {
            const bool unnamed = name == "(unnamed)";
            const std::string text = unnamed ? "An action has no name" : "More than one action is called " + name;
            if (ImGui::Selectable(text.c_str()))
                select(unnamed ? std::string{} : name);
        }
        for (const auto &shared : m_analysis.sharedInputs)
        {
            if (shared.disambiguatedByTriggers)
                continue;
            std::string names;
            for (const auto &name : shared.actions)
                names += (names.empty() ? "" : ", ") + name;
            const std::string text = shared.label + " fires " + names + " together";
            ImGui::PushID(shared.inputId.c_str());
            if (ImGui::Selectable(text.c_str()))
                select(shared.actions.front());
            ImGui::SetItemTooltip("Give the bindings different triggers (tap, hold, double tap) if one press should not do both");
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    void InputMappingEditorPanel::DrawSavePrompt()
    {
        if (std::exchange(m_openSavePrompt, false))
            ImGui::OpenPopup("Unsaved Input Mapping");
        if (!ImGui::BeginPopupModal("Unsaved Input Mapping", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("Save changes to %s?", m_session ? m_session->GetLabel().c_str() : "the mapping");
        const auto proceed = [this]
        {
            const auto pending = std::move(m_pendingReference);
            m_pendingReference.reset();
            m_session.reset();
            if (pending && !pending->empty())
                OpenReference(*pending);
            ImGui::CloseCurrentPopup();
        };
        if (widgets::PrimaryButton("Save"))
        {
            m_error.clear();
            if (!m_session || m_session->Save(m_error))
                proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save"))
            proceed();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_pendingReference.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void InputMappingEditorPanel::HandleShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput || m_actions.capture.IsActive())
            return;
        const auto &io = ImGui::GetIO();
        if (!io.KeyCtrl && !io.KeySuper)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            io.KeyShift ? m_session->Redo() : m_session->Undo();
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            m_session->Redo();
        else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && m_session->IsDirty())
            Save();
    }

    void InputMappingEditorPanel::Save()
    {
        m_error.clear();
        m_session->Save(m_error);
    }
}
