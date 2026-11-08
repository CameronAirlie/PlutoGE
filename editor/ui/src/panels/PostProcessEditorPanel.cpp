#include "PlutoGE/ui/panels/PostProcessEditorPanel.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/postprocess/PostProcessPresetSession.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        void CollectSceneUsers(scene::Entity *entity, const std::string &reference, std::vector<postprocess::PostProcessPresetUser> &users)
        {
            if (auto *camera = entity->GetComponent<scene::CameraComponent>(); camera && camera->GetPostProcessPresetAssetReference() == reference)
            {
                const char *detail = camera->RendersToTexture() ? "Render texture camera"
                                     : camera->IsOverlay()     ? "Overlay camera"
                                     : camera->IsMainCamera()  ? "Main camera"
                                                               : "Camera";
                users.push_back({entity->GetName(), detail, entity->GetID()});
            }
            for (auto *child : entity->GetChildren())
                CollectSceneUsers(child, reference, users);
        }
    }

    PostProcessEditorPanel::PostProcessEditorPanel(const PanelConfig &config) : Panel(config) {}
    PostProcessEditorPanel::~PostProcessEditorPanel() = default;

    void PostProcessEditorPanel::OnProjectChanged()
    {
        m_session.reset();
        m_reference.clear();
        m_followedReference.clear();
        m_pendingReference.reset();
        m_error.clear();
    }

    bool PostProcessEditorPanel::HasUnsavedChanges() const
    {
        return m_session && m_session->IsDirty();
    }

    void PostProcessEditorPanel::ResetViews()
    {
        m_analysis = {};
        m_effects = {};
        m_requestedTab = Tab::Effects;
    }

    void PostProcessEditorPanel::Open(std::unique_ptr<PostProcessPresetSession> session)
    {
        SetOpen(true);
        m_session = std::move(session);
        m_reference.clear();
        m_pendingReference.reset();
        m_error.clear();
        ResetViews();
    }

    void PostProcessEditorPanel::OpenReference(const std::string &reference)
    {
        m_error.clear();
        // Close the old session first so its preview ends before the new one reads the file.
        m_session.reset();
        m_session = PostProcessPresetSession::OpenAsset(reference, m_error);
        m_reference = reference;
        ResetViews();
    }

    void PostProcessEditorPanel::FollowShell()
    {
        const auto &reference = EditorShell::GetInstance().GetActivePostProcessPresetAssetReference();
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

    void PostProcessEditorPanel::Render()
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

        if (ImGui::BeginTabBar("##PostProcessTabs"))
        {
            const auto tab = [this](const char *label, Tab id)
            {
                const ImGuiTabItemFlags flags = m_requestedTab == id ? ImGuiTabItemFlags_SetSelected : 0;
                return ImGui::BeginTabItem(label, nullptr, flags);
            };
            if (tab("Effects", Tab::Effects))
            {
                postprocess::DrawEffectsView(*m_session, m_effects, m_analysis);
                ImGui::EndTabItem();
            }
            if (!m_session->GetReference().empty() && tab("Used By", Tab::Usage))
            {
                DrawUsage();
                ImGui::EndTabItem();
            }
            m_requestedTab.reset();
            ImGui::EndTabBar();
        }
        m_session->EndFrame(ImGui::IsAnyItemActive());
    }

    void PostProcessEditorPanel::DrawEmptyState()
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        widgets::PageTitle("Post Processing", "Choose the effects that give a camera its final look, and tune each one.");
        if (!project)
        {
            ImGui::TextDisabled("Open a project to edit post process presets.");
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
        ImGui::TextWrapped("A preset is a stack of effects such as tone mapping, bloom and ambient occlusion. Cameras choose a "
                           "preset under Post Processing in the Inspector, so one preset can give several cameras, and the "
                           "editor viewport, the same look. Saving a preset updates every camera using it.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        if (RenderProjectAssetPicker("Open preset", project, assets::ProjectAssetType::PostProcessPreset, m_pickerReference) &&
            !m_pickerReference.empty())
            shell.OpenPostProcessPresetAsset(m_pickerReference);
        ImGui::TextDisabled("To make a new one, use Create > Post Process Preset in the Content Browser, or + beside a camera's preset.");
    }

    void PostProcessEditorPanel::DrawToolbar()
    {
        auto &session = *m_session;
        const ImGuiStyle &style = ImGui::GetStyle();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(icons::Quality);
        ImGui::SameLine();
        ImGui::TextUnformatted(session.GetLabel().c_str());
        if (!m_reference.empty())
            ImGui::SetItemTooltip("%s", m_reference.c_str());
        if (session.IsDirty())
        {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled(session.IsLivePreview() ? "(unsaved, previewing)" : "(unsaved)");
        }

        // Actions sit at the right edge.
        const char *liveLabel = "Live";
        const float liveWidth = ImGui::GetFrameHeight() * 1.4f + style.ItemSpacing.x + ImGui::CalcTextSize(liveLabel).x;
        const float width = liveWidth + style.ItemSpacing.x * 2.0f + 5.0f * (ImGui::GetFrameHeight() + style.ItemSpacing.x);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
        bool live = session.IsLivePreview();
        if (widgets::ToggleSwitch("##Live", &live))
            session.SetLivePreview(live);
        ImGui::SetItemTooltip("Show unsaved changes on every camera using this preset. When off, cameras show the saved preset.");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(liveLabel);
        ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
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
        if (widgets::IconButton("##Save", icons::Save, "Save and update every camera using this preset (Ctrl+S)"))
            Save();
        ImGui::SameLine();
        if (widgets::IconButton("##Revert", icons::Revert, "Discard unsaved changes"))
            session.Revert();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (widgets::IconButton("##Close", icons::Close, "Close this preset"))
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

    void PostProcessEditorPanel::DrawProblems()
    {
        if (!m_session || m_analysis.issues.empty())
            return;
        const int warnings = m_analysis.WarningCount();
        const int notes = static_cast<int>(m_analysis.issues.size()) - warnings;
        std::string heading = warnings > 0 ? std::to_string(warnings) + (warnings == 1 ? " problem" : " problems") : std::string{};
        if (notes > 0)
            heading += (heading.empty() ? "" : ", ") + std::to_string(notes) + (notes == 1 ? " note" : " notes");
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(warnings > 0 ? EditorColor::Warning : EditorColor::TextMuted));
        const bool open = ImGui::TreeNode("##Problems", "%s  %s", warnings > 0 ? icons::Warning : icons::Settings, heading.c_str());
        ImGui::PopStyleColor();
        if (!open)
            return;
        for (int index = 0; index < static_cast<int>(m_analysis.issues.size()); ++index)
        {
            const auto &issue = m_analysis.issues[index];
            const bool warning = issue.severity == postprocess::PostProcessIssue::Severity::Warning;
            ImGui::PushID(index);
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(warning ? EditorColor::Text : EditorColor::TextMuted));
            if (ImGui::Selectable(issue.message.c_str()) && issue.effect >= 0)
            {
                m_effects.selected = issue.effect;
                m_requestedTab = Tab::Effects;
            }
            ImGui::PopStyleColor();
            if (issue.effect >= 0)
                ImGui::SetItemTooltip("Show the effect");
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    void PostProcessEditorPanel::DrawUsage()
    {
        auto &shell = EditorShell::GetInstance();
        const std::string reference = m_session->GetReference();
        std::vector<postprocess::PostProcessPresetUser> users;
        const bool editorCameraUses = shell.GetEditorCamera().GetPostProcessPresetAssetReference() == reference;
        if (editorCameraUses)
            users.push_back({"Editor Camera", "Editor viewport", 0});
        if (auto *scene = shell.GetScene())
            for (auto *root : scene->GetRootEntities())
                CollectSceneUsers(root, reference, users);

        bool assignToEditorCamera = false;
        if (const auto clicked = postprocess::DrawUsageView(users, editorCameraUses, assignToEditorCamera))
        {
            const auto &user = users[*clicked];
            if (user.entity == 0)
                shell.SelectEditorCamera();
            else if (auto *scene = shell.GetScene())
                shell.SetSelectedEntity(scene->FindEntityByID(user.entity));
        }
        if (assignToEditorCamera && shell.GetEditorCamera().SetPostProcessPresetAssetReference(reference))
            shell.MarkProjectDirty();
    }

    void PostProcessEditorPanel::DrawSavePrompt()
    {
        if (std::exchange(m_openSavePrompt, false))
            ImGui::OpenPopup("Unsaved Post Process Preset");
        if (!ImGui::BeginPopupModal("Unsaved Post Process Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("Save changes to %s?", m_session ? m_session->GetLabel().c_str() : "the preset");
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

    void PostProcessEditorPanel::HandleShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
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

    std::string PostProcessEditorPanel::GetDocumentName() const
    {
        return m_session ? "Post-process preset " + m_session->GetLabel() : std::string("Post-process preset");
    }

    bool PostProcessEditorPanel::SaveDocument(std::string &error)
    {
        return !m_session || m_session->Save(error);
    }

    void PostProcessEditorPanel::DiscardChanges()
    {
        if (m_session)
            m_session->Revert();
    }

    void PostProcessEditorPanel::Save()
    {
        m_error.clear();
        m_session->Save(m_error);
    }
}
