#include "PlutoGE/ui/panels/UIThemeEditorPanel.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/scene/ui/UITheme.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace PlutoGE::ui
{
    namespace
    {
        const ImVec4 kErrorColor(1.0f, 0.55f, 0.45f, 1.0f);
    }

    UIThemeEditorPanel::UIThemeEditorPanel(const PanelConfig &config) : Panel(config) {}
    UIThemeEditorPanel::~UIThemeEditorPanel() = default;

    void UIThemeEditorPanel::OnProjectChanged()
    {
        m_document.reset();
        m_pendingOpen.reset();
        m_error.clear();
    }

    bool UIThemeEditorPanel::HasUnsavedChanges() const
    {
        return m_document && m_document->IsDirty();
    }

    void UIThemeEditorPanel::Open(const UIThemeTarget &target, const std::string &style)
    {
        SetOpen(true);
        ImGui::SetWindowFocus(GetName().c_str());
        if (m_document && m_document->GetTarget() == target)
        {
            if (!style.empty())
                m_styles.selected = style;
            m_requestedTab = Tab::Styles;
            return;
        }
        if (HasUnsavedChanges())
        {
            m_pendingOpen = std::make_pair(target, style);
            m_openSavePrompt = true;
            return;
        }
        OpenNow(target, style);
    }

    void UIThemeEditorPanel::Open(std::unique_ptr<UIThemeDocument> document)
    {
        SetOpen(true);
        m_pendingOpen.reset();
        m_error.clear();
        m_document = std::move(document);
        m_styles = {};
        m_variables = {};
        m_source = {};
        m_requestedTab = Tab::Styles;
    }

    void UIThemeEditorPanel::OpenNow(const UIThemeTarget &target, const std::string &style)
    {
        m_error.clear();
        m_document = UIThemeDocument::Open(target, m_error);
        m_styles = {};
        m_styles.selected = style;
        m_variables = {};
        m_source = {};
        m_requestedTab = Tab::Styles;
    }

    void UIThemeEditorPanel::Render()
    {
        if (m_document && !m_document->IsValid())
            m_document.reset();
        DrawSavePrompt();
        if (!m_document)
        {
            if (EditorShell::GetInstance().GetProject())
                DrawEmptyState();
            else
                ImGui::TextDisabled("Open a project to edit UI themes.");
            return;
        }

        HandleShortcuts();
        DrawToolbar();
        DrawBaseThemes();
        DrawProblems();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##ThemeTabs"))
        {
            const auto tab = [this](const char *label, Tab id)
            {
                const ImGuiTabItemFlags flags = m_requestedTab == id ? ImGuiTabItemFlags_SetSelected : 0;
                return ImGui::BeginTabItem(label, nullptr, flags);
            };
            if (tab("Styles", Tab::Styles))
            {
                theme::DrawStylesView(*m_document, m_styles);
                ImGui::EndTabItem();
            }
            if (tab("Variables", Tab::Variables))
            {
                theme::DrawVariablesView(*m_document, m_variables);
                ImGui::EndTabItem();
            }
            if (tab("Source", Tab::Source))
            {
                theme::DrawSourceView(*m_document, m_source);
                ImGui::EndTabItem();
            }
            m_requestedTab.reset();
            ImGui::EndTabBar();
        }
        m_document->EndFrame();
    }

    void UIThemeEditorPanel::DrawEmptyState()
    {
        auto *project = EditorShell::GetInstance().GetProject();
        widgets::PageTitle("UI Themes", "Styles that give every element of your UI a consistent look.");
        ImGui::TextWrapped("A theme holds named styles (Button, Heading, Panel...) and shared variables such as your accent "
                           "colour. Put a UI Theme component on a canvas, give elements a UI Style component, and pick styles "
                           "from the theme. Styles can be based on other styles and change per state (hover, pressed...).");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        if (RenderProjectAssetPicker("Open theme", project, assets::ProjectAssetType::UITheme, m_pickerReference) &&
            !m_pickerReference.empty())
            Open(UIThemeTarget::ForAsset(m_pickerReference));
        if (widgets::PrimaryButton("New Theme"))
        {
            const auto reference = CreateUIThemeAsset(GetDefaultUIThemeDirectory(), "Theme", scene::MakeStarterUITheme(), m_error);
            if (!reference.empty())
                Open(UIThemeTarget::ForAsset(reference));
        }
        ImGui::SetItemTooltip("Creates a theme in UI/Themes with ready-made styles to start from");
        if (!m_error.empty())
            ImGui::TextColored(kErrorColor, "%s", m_error.c_str());
    }

    void UIThemeEditorPanel::DrawToolbar()
    {
        auto &document = *m_document;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(icons::Palette);
        ImGui::SameLine();
        ImGui::TextUnformatted(document.GetLabel().c_str());
        if (document.IsDirty())
        {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled("(unsaved)");
        }
        if (!document.SavesSeparately())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("- saved with the scene");
        }

        // Actions sit at the right edge.
        const float buttons = document.SavesSeparately() ? 5.0f : 3.0f;
        const float width = buttons * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
        ImGui::BeginDisabled(!document.CanUndo());
        if (widgets::IconButton("##Undo", icons::ArrowLeft, "Undo (Ctrl+Z)"))
            document.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document.CanRedo());
        if (widgets::IconButton("##Redo", icons::ArrowRight, "Redo (Ctrl+Y)"))
            document.Redo();
        ImGui::EndDisabled();
        if (document.SavesSeparately())
        {
            ImGui::SameLine();
            ImGui::BeginDisabled(!document.IsDirty());
            if (widgets::IconButton("##Save", icons::Save, "Save (Ctrl+S)"))
                document.Save(m_error);
            ImGui::SameLine();
            if (widgets::IconButton("##Revert", icons::Revert, "Discard unsaved changes"))
                document.Revert();
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (widgets::IconButton("##Close", icons::Close, "Close this theme"))
        {
            if (HasUnsavedChanges())
            {
                m_pendingOpen = std::make_pair(UIThemeTarget{}, std::string{});
                m_openSavePrompt = true;
            }
            else
                m_document.reset();
        }
        if (!m_error.empty())
            ImGui::TextColored(kErrorColor, "%s", m_error.c_str());
    }

    void UIThemeEditorPanel::DrawBaseThemes()
    {
        auto &document = *m_document;
        auto *project = EditorShell::GetInstance().GetProject();
        const auto &bases = document.GetTheme().baseThemes;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Based on");
        ImGui::SetItemTooltip("Themes whose styles and variables this one inherits and can override");
        for (std::size_t index = 0; index < bases.size(); ++index)
        {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(index));
            const auto label = std::filesystem::path(bases[index]).stem().string() + "  " + icons::Close;
            if (ImGui::SmallButton(label.c_str()))
                document.Modify([index](scene::UITheme &theme) { theme.baseThemes.erase(theme.baseThemes.begin() + static_cast<std::ptrdiff_t>(index)); });
            ImGui::SetItemTooltip("%s\nClick to remove", bases[index].c_str());
            ImGui::PopID();
        }
        if (bases.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("nothing");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        m_baseThemePicker.clear();
        if (RenderProjectAssetPicker("##AddBase", project, assets::ProjectAssetType::UITheme, m_baseThemePicker,
                                     {.noneLabel = "+ base theme"}) &&
            !m_baseThemePicker.empty())
        {
            const auto &target = document.GetTarget();
            if (target.kind == UIThemeTarget::Kind::Asset && target.asset == m_baseThemePicker)
                m_error = "A theme cannot be based on itself";
            else if (std::find(bases.begin(), bases.end(), m_baseThemePicker) == bases.end())
                document.Modify([reference = m_baseThemePicker](scene::UITheme &theme) { theme.baseThemes.push_back(reference); });
        }
    }

    void UIThemeEditorPanel::DrawProblems()
    {
        auto &document = *m_document;
        const auto &loadErrors = document.GetLoadErrors();
        const auto &errors = document.GetResolved().GetErrors();
        if (loadErrors.empty() && errors.empty())
            return;
        const auto count = loadErrors.size() + errors.size();
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        const bool open = ImGui::TreeNode("##Problems", "%s  %zu problem%s", icons::Warning, count, count == 1 ? "" : "s");
        ImGui::PopStyleColor();
        if (!open)
            return;
        if (!loadErrors.empty())
            ImGui::TextDisabled("These lines could not be read and will be dropped when the theme is saved:");
        for (const auto &error : loadErrors)
            ImGui::BulletText("%s", error.c_str());
        for (const auto &error : errors)
            ImGui::BulletText("%s", error.c_str());
        ImGui::TreePop();
    }

    void UIThemeEditorPanel::DrawSavePrompt()
    {
        if (std::exchange(m_openSavePrompt, false))
            ImGui::OpenPopup("Unsaved Theme");
        if (!ImGui::BeginPopupModal("Unsaved Theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("Save changes to %s?", m_document ? m_document->GetLabel().c_str() : "the theme");
        const auto proceed = [this]
        {
            const auto pending = std::move(m_pendingOpen);
            m_pendingOpen.reset();
            m_document.reset();
            if (pending && (pending->first.kind == UIThemeTarget::Kind::LocalStyles || !pending->first.asset.empty()))
                OpenNow(pending->first, pending->second);
            ImGui::CloseCurrentPopup();
        };
        if (widgets::PrimaryButton("Save"))
        {
            if (!m_document || m_document->Save(m_error))
                proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save"))
            proceed();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_pendingOpen.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void UIThemeEditorPanel::HandleShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
            return;
        const auto &io = ImGui::GetIO();
        const bool command = io.KeyCtrl || io.KeySuper;
        if (!command)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            io.KeyShift ? m_document->Redo() : m_document->Undo();
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            m_document->Redo();
        else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && m_document->IsDirty())
            m_document->Save(m_error);
    }
}
