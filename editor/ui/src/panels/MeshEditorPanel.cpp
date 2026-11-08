#include "PlutoGE/ui/panels/MeshEditorPanel.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/meshedit/MeshAssetSession.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace PlutoGE::ui
{
    using meshedit::MeshEditorPage;

    MeshEditorPanel::MeshEditorPanel(const PanelConfig &config) : Panel(config) {}
    MeshEditorPanel::~MeshEditorPanel() = default;

    void MeshEditorPanel::OnProjectChanged()
    {
        m_session.reset();
        m_reference.clear();
        m_followedReference.clear();
        m_pendingReference.reset();
        m_error.clear();
    }

    bool MeshEditorPanel::HasUnsavedChanges() const
    {
        return m_session && m_session->IsDirty();
    }

    void MeshEditorPanel::ResetViews()
    {
        m_rigAnalysis = {};
        m_rig = {};
    }

    void MeshEditorPanel::Open(std::unique_ptr<MeshAssetSession> session)
    {
        SetOpen(true);
        m_session = std::move(session);
        m_reference.clear();
        m_pendingReference.reset();
        m_error.clear();
        ResetViews();
    }

    void MeshEditorPanel::OpenReference(const std::string &reference)
    {
        m_error.clear();
        m_session = MeshAssetSession::OpenAsset(reference, m_error);
        m_reference = reference;
        ResetViews();
    }

    void MeshEditorPanel::FollowShell()
    {
        const auto &reference = EditorShell::GetInstance().GetActiveMeshAssetReference();
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

    void MeshEditorPanel::Render()
    {
        FollowShell();
        DrawSavePrompt();
        if (!m_session)
        {
            DrawEmptyState();
            return;
        }

        HandleShortcuts();
        m_rigAnalysis.Update(*m_session);
        DrawToolbar();
        ImGui::Spacing();

        const float height = ImGui::GetContentRegionAvail().y;
        DrawNavigation(height);
        ImGui::SameLine(0.0f, 0.0f);
        const ImVec2 dividerTop = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(dividerTop, ImVec2(dividerTop.x, dividerTop.y + height), GetEditorColorU32(EditorColor::Border));

        // The rig page fills the space with its diagram; the others scroll.
        const bool fill = m_page == MeshEditorPage::HumanoidRig;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
        ImGui::BeginChild("##MeshEditorPage", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                          fill ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
        ImGui::PopStyleVar();
        DrawPage();
        ImGui::EndChild();

        m_session->EndFrame(ImGui::IsAnyItemActive());
    }

    void MeshEditorPanel::DrawEmptyState()
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        widgets::PageTitle("Mesh Editor", "Materials, humanoid rig and level of detail for a mesh asset.");
        if (!project)
        {
            ImGui::TextDisabled("Open a project to edit meshes.");
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
        std::string picked;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        if (RenderProjectAssetPicker("Open mesh", project, assets::ProjectAssetType::Mesh, picked, {.includeEngineAssets = true}) && !picked.empty())
            shell.OpenMeshAsset(picked);
        ImGui::TextDisabled("Or double-click a mesh in the Content Browser.");
    }

    void MeshEditorPanel::DrawToolbar()
    {
        auto &session = *m_session;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(icons::MeshAsset);
        ImGui::SameLine();
        ImGui::TextUnformatted(session.GetLabel().c_str());
        if (!session.GetReference().empty())
            ImGui::SetItemTooltip("%s", session.GetReference().c_str());
        if (session.IsReadOnly())
        {
            ImGui::SameLine();
            widgets::Pill("Read-only", GetEditorColor(EditorColor::TextMuted));
            ImGui::SetItemTooltip("%s", session.GetReadOnlyReason().c_str());
        }
        else if (session.IsDirty())
        {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled("(unsaved)");
        }

        // Actions at the right edge.
        const ImGuiStyle &style = ImGui::GetStyle();
        const float width = 5.0f * (ImGui::GetFrameHeight() + style.ItemSpacing.x);
        ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
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
        ImGui::BeginDisabled(!session.IsDirty() || session.IsReadOnly());
        if (widgets::IconButton("##Save", icons::Save, "Save and update every instance in the open scene (Ctrl+S)"))
            Save();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!session.IsDirty());
        if (widgets::IconButton("##Revert", icons::Revert, "Discard unsaved changes"))
            Revert();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (widgets::IconButton("##Close", icons::Close, "Close this mesh"))
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

    void MeshEditorPanel::DrawNavigation(float height)
    {
        // Sidebar, matching Project Settings and Preferences.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, GetEditorColor(EditorColor::Background));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
        ImGui::BeginChild("##MeshEditorNav", ImVec2(ImGui::GetFontSize() * 12.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::TextUnformatted("MESH");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        const auto nav = [&](const char *icon, const char *label, MeshEditorPage page)
        {
            if (widgets::NavItem(icon, label, m_page == page))
                m_page = page;
        };
        nav(icons::MeshAsset, "Overview", MeshEditorPage::Overview);
        nav(icons::MaterialAsset, "Materials", MeshEditorPage::Materials);
        nav(icons::EntitySkinnedMesh, "Humanoid Rig", MeshEditorPage::HumanoidRig);
        nav(icons::Quality, "Level of Detail", MeshEditorPage::LevelOfDetail);

        // The rig is the page most likely to need attention; say so here.
        const auto &skeleton = m_session->GetSkeleton();
        if (!skeleton.humanoidBoneMappings.empty() && !m_rigAnalysis.status.IsReady())
        {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
            ImGui::TextWrapped("%s  The humanoid rig needs attention.", icons::Warning);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void MeshEditorPanel::DrawPage()
    {
        auto &shell = EditorShell::GetInstance();
        switch (m_page)
        {
        case MeshEditorPage::Overview:
            meshedit::DrawOverviewPage(*m_session, m_rigAnalysis, m_page);
            break;
        case MeshEditorPage::Materials:
            meshedit::DrawMaterialsPage(*m_session, shell.GetProject());
            break;
        case MeshEditorPage::HumanoidRig:
            meshedit::DrawHumanoidRigPage(*m_session, m_rig, m_rigAnalysis);
            break;
        case MeshEditorPage::LevelOfDetail:
            meshedit::DrawLevelOfDetailPage(*m_session, m_error);
            break;
        }
    }

    void MeshEditorPanel::DrawSavePrompt()
    {
        if (std::exchange(m_openSavePrompt, false))
            ImGui::OpenPopup("Unsaved Mesh");
        if (!ImGui::BeginPopupModal("Unsaved Mesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("Save changes to %s?", m_session ? m_session->GetLabel().c_str() : "the mesh");
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
            Save();
            if (m_error.empty())
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

    void MeshEditorPanel::HandleShortcuts()
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
        else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && m_session->IsDirty() && !m_session->IsReadOnly())
            Save();
    }

    std::string MeshEditorPanel::GetDocumentName() const
    {
        return m_session ? "Mesh " + m_session->GetLabel() : std::string("Mesh");
    }

    bool MeshEditorPanel::SaveDocument(std::string &error)
    {
        Save();
        error = m_error;
        return !m_session || !m_session->IsDirty();
    }

    void MeshEditorPanel::Save()
    {
        m_error.clear();
        if (!m_session || !m_session->IsDirty())
            return;
        auto &shell = EditorShell::GetInstance();
        if (!m_session->Save(m_error))
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, m_error.empty() ? "Failed to save mesh asset." : m_error);
            return;
        }
        shell.MarkProjectDirty();
        shell.MarkSceneDirty();
        shell.Log(EditorShell::ConsoleSeverity::Info, "Saved mesh asset: " + m_session->GetReference());
    }

    void MeshEditorPanel::Revert()
    {
        m_error.clear();
        if (m_session)
            m_session->Revert(m_error);
    }
}
