#include "PlutoGE/ui/panels/FoliageEditorPanel.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/SceneWind.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/FoliageDiagnostics.h"
#include "PlutoGE/ui/FoliageEditTool.h"
#include "PlutoGE/ui/FoliageEditing.h"
#include "PlutoGE/ui/FoliageTypeSections.h"
#include "PlutoGE/ui/FoliageTypeUsage.h"

#include <imgui.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kOverviewWidth = 260.0f;
        // Below this the overview and the settings share one scrolling column.
        constexpr float kMinimumTwoColumnWidth = 640.0f;
    }

    FoliageEditorPanel::FoliageEditorPanel(const PanelConfig &config) : Panel(config) {}
    FoliageEditorPanel::~FoliageEditorPanel() = default;

    void FoliageEditorPanel::Shutdown()
    {
        m_session.reset();
    }

    void FoliageEditorPanel::OnProjectChanged()
    {
        CloseSession();
        m_reference.clear();
        m_pendingReference.reset();
        m_error.clear();
    }

    bool FoliageEditorPanel::HasUnsavedChanges() const
    {
        return m_session && m_session->IsDirty();
    }

    void FoliageEditorPanel::OpenAsset(const std::string &reference)
    {
        if (reference.empty()) return;
        SetOpen(true);
        // By name, not from Render, which does not run while the panel is a hidden dock tab.
        ImGui::SetWindowFocus(GetName().c_str());
        if (reference == m_reference && m_session) return;
        if (HasUnsavedChanges())
        {
            m_pendingReference = reference;
            m_openSavePrompt = true;
            return;
        }
        OpenReference(reference);
    }

    void FoliageEditorPanel::Open(std::unique_ptr<FoliageTypeSession> session)
    {
        SetOpen(true);
        CloseSession();
        m_session = std::move(session);
        m_reference.clear();
        m_pendingReference.reset();
        m_error.clear();
    }

    void FoliageEditorPanel::OpenReference(const std::string &reference)
    {
        m_error.clear();
        // Close the old session first so its preview ends before the new one reads a linked type.
        CloseSession();
        m_reference = reference;
        m_session = FoliageTypeSession::Open(MakeFoliageTypeAssetStorage(reference, [] { return EditorShell::GetInstance().GetScene(); }), m_error);
    }

    void FoliageEditorPanel::CloseSession()
    {
        m_session.reset();
        m_meshReference.clear();
        m_mesh = nullptr;
        m_hasImpostor = false;
        m_previewedScene = nullptr;
        m_closeRequested = false;
    }

    void FoliageEditorPanel::UpdatePreviewState()
    {
        auto &shell = EditorShell::GetInstance();
        // The game owns its scene while it runs; stopping restores the scene from the saved file, and this reapplies.
        m_session->SetPreviewSuspended(shell.GetEngine().IsRuntimeRunning());
        if (const auto *scene = shell.GetScene(); scene != m_previewedScene)
        {
            m_previewedScene = scene;
            m_session->ReapplyPreview();
        }
    }

    void FoliageEditorPanel::UpdateMesh()
    {
        const auto &reference = m_session->GetAsset().sourceMeshPath;
        if (m_meshReference == reference) return;
        m_meshReference = reference;
        m_mesh = nullptr;
        m_hasImpostor = false;
        if (reference.empty()) return;
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        // An import can fail on a bad file. The problems list reports the missing mesh rather than failing the panel.
        try
        {
            m_mesh = assets.LoadMeshAsset(reference);
            m_hasImpostor = assets.GetMeshAssetMetadata(reference).impostor.has_value();
        }
        catch (const std::exception &)
        {
            m_mesh = nullptr;
        }
    }

    void FoliageEditorPanel::Render()
    {
        // Deferred from the toolbar, because the rest of the frame still draws the session.
        if (m_closeRequested)
        {
            CloseSession();
            m_reference.clear();
        }
        DrawSavePrompt();
        if (!m_session)
        {
            DrawEmptyState();
            return;
        }

        HandleShortcuts();
        UpdatePreviewState();
        UpdateMesh();
        DrawToolbar();
        const auto issues = DiagnoseFoliageAsset(m_session->GetAsset(), m_mesh, m_hasImpostor, true);
        DrawFoliageIssues(issues);
        ImGui::Spacing();

        if (ImGui::GetContentRegionAvail().x >= kMinimumTwoColumnWidth)
        {
            if (ImGui::BeginChild("##Overview", ImVec2(kOverviewWidth, 0.0f)))
                DrawOverview();
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("##Settings"))
                DrawSettings();
            ImGui::EndChild();
        }
        else
        {
            if (ImGui::BeginChild("##Column"))
            {
                DrawOverview();
                ImGui::Spacing();
                DrawSettings();
            }
            ImGui::EndChild();
        }
        m_session->EndFrame(ImGui::IsAnyItemActive());
    }

    void FoliageEditorPanel::DrawEmptyState()
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        widgets::PageTitle("Foliage Types", "Define a plant once, then paint it in any scene.");
        if (!project)
        {
            ImGui::TextDisabled("Open a project to edit foliage types.");
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
        ImGui::TextWrapped("A foliage type says what a plant draws and how it is placed, rendered, collided and moved by wind. Foliage "
                           "components link to it from their type list, so changing it here updates every plant using it.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        if (RenderProjectAssetPicker("Open foliage type", project, assets::ProjectAssetType::FoliageType, m_pickerReference) &&
            !m_pickerReference.empty())
            OpenAsset(std::exchange(m_pickerReference, {}));
        ImGui::TextDisabled("To make a new one, use Create > Rendering > Foliage Type in the Content Browser,");
        ImGui::TextDisabled("Create Foliage Type on a mesh, or Save as Asset on a type in the Inspector.");
    }

    void FoliageEditorPanel::DrawToolbar()
    {
        auto &session = *m_session;
        const ImGuiStyle &style = ImGui::GetStyle();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(icons::EntityFoliage);
        ImGui::SameLine();
        ImGui::TextUnformatted(session.GetLabel().c_str());
        if (!m_reference.empty())
            ImGui::SetItemTooltip("%s", m_reference.c_str());
        if (session.IsDirty())
        {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled(session.IsPreviewing() ? "(unsaved, previewing)" : "(unsaved)");
        }

        // Actions sit at the right edge, or on their own line when the name leaves no room.
        const char *liveLabel = "Live";
        const float liveWidth = ImGui::GetFrameHeight() * 1.4f + style.ItemSpacing.x + ImGui::CalcTextSize(liveLabel).x;
        const float width = liveWidth + style.ItemSpacing.x * 2.0f + 6.0f * (ImGui::GetFrameHeight() + style.ItemSpacing.x);
        const float actionsX = ImGui::GetContentRegionMax().x - width;
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() <= actionsX)
            ImGui::SameLine(actionsX);
        else
            ImGui::NewLine();
        bool live = session.IsLivePreview();
        if (widgets::ToggleSwitch("##Live", &live))
            session.SetLivePreview(live);
        ImGui::SetItemTooltip("Show unsaved changes on every foliage type using this asset. When off, they show the saved asset.");
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
        if (widgets::IconButton("##Save", icons::Save, "Save and update every foliage type using this asset (Ctrl+S)"))
            Save();
        ImGui::SameLine();
        if (widgets::IconButton("##Revert", icons::Revert, "Discard unsaved changes"))
            session.Revert();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_reference.empty());
        if (widgets::IconButton("##Reveal", icons::FolderOpen, "Show in the Content Browser"))
            EditorShell::GetInstance().RevealAsset(m_reference);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (widgets::IconButton("##Close", icons::Close, "Close this foliage type"))
        {
            if (HasUnsavedChanges())
            {
                m_pendingReference = std::string{};
                m_openSavePrompt = true;
            }
            else
                m_closeRequested = true;
        }
        if (!m_error.empty())
            ImGui::TextColored(GetEditorColor(EditorColor::Error), "%s", m_error.c_str());
    }

    void FoliageEditorPanel::DrawOverview()
    {
        const auto &mesh = m_session->GetAsset().sourceMeshPath;
        const float side = (std::min)(ImGui::GetContentRegionAvail().x, kOverviewWidth);
        const auto thumbnail = mesh.empty() ? 0 : EditorShell::GetInstance().GetAssetThumbnail(mesh);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + side, min.y + side);
        ImGui::Dummy(ImVec2(side, side));
        auto *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
        if (thumbnail)
        {
            drawList->AddImage(static_cast<ImTextureID>(thumbnail), min, max, ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            const char *text = mesh.empty() ? "No mesh" : "No preview";
            const ImVec2 size = ImGui::CalcTextSize(text);
            drawList->AddText(ImVec2(min.x + (side - size.x) * 0.5f, min.y + (side - size.y) * 0.5f),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        }
        ImGui::Spacing();
        if (!m_session->GetReference().empty())
            DrawUsage();
    }

    void FoliageEditorPanel::DrawUsage()
    {
        auto &shell = EditorShell::GetInstance();
        auto *scene = shell.GetScene();
        if (!scene) return;
        const std::string reference = m_session->GetReference();
        const auto users = FindFoliageTypeUsers(*scene, reference);

        auto *selected = shell.GetSelectedEntity();
        auto *selectedFoliage = selected ? selected->GetComponent<scene::FoliageComponent>() : nullptr;
        FoliageUsageViewOptions options;
        options.editable = !shell.GetEngine().IsRuntimeRunning();
        if (selectedFoliage) options.addTargetName = selected->GetName();

        const auto result = DrawFoliageUsageView(users, options);
        const auto showUser = [&](std::size_t index) -> scene::Entity *
        {
            auto *entity = scene->FindEntityByID(users[index].entity);
            if (!entity) return nullptr;
            shell.SetSelectedEntity(entity);
            if (auto *foliage = entity->GetComponent<scene::FoliageComponent>())
                foliage->SetSelectedTypeIndex(static_cast<int>(users[index].typeIndex));
            return entity;
        };
        if (result.show)
            showUser(*result.show);
        if (result.paint)
            if (const auto *entity = showUser(*result.paint))
                shell.GetFoliageTool().SetMode(FoliageToolMode::Paint, entity->GetID());
        if (result.addToTarget && selectedFoliage && AddFoliageTypeFromAsset(shell, *selected, *selectedFoliage, reference))
            m_session->ReapplyPreview(); // The new type read the saved file.
    }

    void FoliageEditorPanel::DrawSettings()
    {
        auto &shell = EditorShell::GetInstance();
        FoliageTypeSectionContext context;
        context.project = shell.GetProject();
        context.mesh = m_mesh;
        context.hasImpostor = m_hasImpostor;
        const auto *scene = shell.GetScene();
        context.sceneHasWind = !scene || !render::BuildSceneWind(scene).Calm();

        auto edited = m_session->GetAsset();
        if (DrawFoliageTypeSections(edited, context))
            m_session->Modify([&](scene::FoliageTypeAsset &asset) { asset = edited; });
    }

    void FoliageEditorPanel::DrawSavePrompt()
    {
        if (std::exchange(m_openSavePrompt, false))
            ImGui::OpenPopup("Unsaved Foliage Type");
        if (!ImGui::BeginPopupModal("Unsaved Foliage Type", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("Save changes to %s?", m_session ? m_session->GetLabel().c_str() : "the foliage type");
        const auto proceed = [this]
        {
            const auto pending = std::exchange(m_pendingReference, std::nullopt);
            CloseSession();
            m_reference.clear();
            if (pending && !pending->empty())
                OpenReference(*pending);
            ImGui::CloseCurrentPopup();
        };
        if (widgets::PrimaryButton("Save"))
        {
            if (!m_session || Save())
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

    void FoliageEditorPanel::HandleShortcuts()
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

    std::string FoliageEditorPanel::GetDocumentName() const
    {
        return m_session ? "Foliage type " + m_session->GetLabel() : std::string("Foliage type");
    }

    bool FoliageEditorPanel::SaveDocument(std::string &error)
    {
        if (!m_session || Save())
            return true;
        error = m_error;
        return false;
    }

    void FoliageEditorPanel::DiscardChanges()
    {
        if (m_session)
            m_session->Revert();
    }

    bool FoliageEditorPanel::Save()
    {
        m_error.clear();
        if (!m_session->Save(m_error))
            return false;

        auto &shell = EditorShell::GetInstance();
        shell.MarkProjectDirty();
        const std::string reference = m_session->GetReference();
        auto *scene = shell.GetScene();
        const auto updated = scene && !reference.empty() ? FindFoliageTypeUsers(*scene, reference).size() : 0;
        if (updated > 0)
        {
            // Linked types keep a copy of their settings in the scene, used when the file cannot be read.
            shell.MarkSceneDirty();
            shell.Log(EditorShell::ConsoleSeverity::Info,
                      "Saved " + reference + " and updated " + std::to_string(updated) + " foliage type(s) in the scene.");
        }
        return true;
    }
}
