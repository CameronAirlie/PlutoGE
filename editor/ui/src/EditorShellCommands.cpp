// Editor commands: registration, per-frame binding, shortcut dispatch, the command palette and opening assets.

#include "PlutoGE/ui/EditorShell.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorPreferences.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/theme/UIThemeDocument.h"
#include "PlutoGE/ui/panels/AnimationGraphEditorPanel.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include "PlutoGE/ui/panels/FoliageEditorPanel.h"
#include "PlutoGE/ui/panels/InputMappingEditorPanel.h"
#include "PlutoGE/ui/panels/PostProcessEditorPanel.h"
#include "PlutoGE/ui/panels/ProfilerPanel.h"
#include "PlutoGE/ui/panels/RmlDocumentEditorPanel.h"
#include "PlutoGE/ui/panels/UIThemeEditorPanel.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"

#include <algorithm>
#include <cfloat>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr ImGuiKeyChord Ctrl(ImGuiKey key) { return ImGuiMod_Ctrl | key; }
        constexpr ImGuiKeyChord CtrlShift(ImGuiKey key) { return ImGuiMod_Ctrl | ImGuiMod_Shift | key; }
        constexpr ImGuiKeyChord CtrlAlt(ImGuiKey key) { return ImGuiMod_Ctrl | ImGuiMod_Alt | key; }

        std::string PanelLabel(const std::string &name)
        {
            return name.substr(0, name.find("##"));
        }
    }

    void EditorShell::RegisterEditorCommands()
    {
        using Scope = EditorCommandScope;
        const auto add = [&](std::string id, std::string label, std::string category, ImGuiKeyChord shortcut, Scope scope = Scope::Global)
        {
            m_commands.Register({.id = std::move(id), .label = std::move(label), .category = std::move(category), .defaultShortcut = shortcut, .scope = scope});
        };

        add("file.newProject", "New Project...", "File", Ctrl(ImGuiKey_N));
        add("file.openProject", "Open Project...", "File", Ctrl(ImGuiKey_O));
        add("file.save", "Save", "File", Ctrl(ImGuiKey_S));
        add("file.newScene", "New Scene", "File", CtrlAlt(ImGuiKey_N));
        add("file.openScene", "Open Scene...", "File", CtrlAlt(ImGuiKey_O));
        add("file.saveScene", "Save Scene", "File", CtrlAlt(ImGuiKey_S));
        add("file.saveSceneAs", "Save Scene As...", "File", CtrlShift(ImGuiKey_S));

        m_commands.Register({.id = "edit.undo", .label = "Undo", .category = "Edit", .defaultShortcut = Ctrl(ImGuiKey_Z), .scope = Scope::Edit, .repeat = true});
        m_commands.Register({.id = "edit.redo", .label = "Redo", .category = "Edit", .defaultShortcut = Ctrl(ImGuiKey_Y),
                             .alternateShortcut = CtrlShift(ImGuiKey_Z), .scope = Scope::Edit, .repeat = true});
        add("scene.copy", "Copy Entity", "Edit", Ctrl(ImGuiKey_C), Scope::Scene);
        add("scene.paste", "Paste Entity", "Edit", Ctrl(ImGuiKey_V), Scope::Scene);
        add("scene.duplicate", "Duplicate Entity", "Edit", Ctrl(ImGuiKey_D), Scope::Scene);
        add("scene.delete", "Delete Entity", "Edit", ImGuiKey_Delete, Scope::Scene);
        add("scene.deselect", "Clear Selection", "Edit", ImGuiKey_Escape, Scope::Scene);
        add("scene.dropToGround", "Drop Selection onto Ground...", "Edit", ImGuiKey_None);

        add("play.play", "Play", "Play", ImGuiKey_F5, Scope::Editor);
        add("play.stop", "Stop", "Play", ImGuiMod_Shift | ImGuiKey_F5, Scope::Editor);
        m_commands.Register({.id = "play.pause", .label = "Pause or Resume", .category = "Play", .defaultShortcut = CtrlShift(ImGuiKey_P),
                             .alternateShortcut = ImGuiKey_Pause, .scope = Scope::Editor});
        add("play.step", "Step Frame", "Play", CtrlAlt(ImGuiKey_P), Scope::Editor);

        add("view.gameViewportFullscreen", "Toggle Game Viewport Fullscreen", "View", ImGuiKey_F11);

        add("build.build", "Build Project...", "Build", Ctrl(ImGuiKey_B));
        add("build.buildAndRun", "Build and Run Project...", "Build", CtrlShift(ImGuiKey_B));
        add("build.scripts", "Build Scripts", "Build", ImGuiKey_None);

        m_commands.Register({.id = "editor.commandPalette", .label = "Command Palette...", .category = "Editor", .defaultShortcut = Ctrl(ImGuiKey_P),
                             .showInPalette = false});
        add("editor.preferences", "Preferences...", "Editor", Ctrl(ImGuiKey_Comma));
        add("editor.keyboardShortcuts", "Keyboard Shortcuts...", "Editor", ImGuiKey_None);
        add("profiler.copyMetrics", "Copy Profiler Metrics", "Editor", CtrlShift(ImGuiKey_C), Scope::Edit);
    }

    void EditorShell::BindEditorCommands(bool isRuntimeRunning, ProfilerPanel *profilerPanel)
    {
        // Shortcuts stay out of the way of typing, menus and editors that use the keyboard themselves.
        const ImGuiIO &io = ImGui::GetIO();
        const bool global = !io.WantTextInput && !(m_rmlDocumentEditor && m_rmlDocumentEditor->OwnsKeyboardFocus()) &&
                            !(m_animationGraphEditor && m_animationGraphEditor->OwnsKeyboardFocus()) &&
                            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool editor = global && !(m_uiThemeEditor && m_uiThemeEditor->HasKeyboardFocus()) &&
                            !(m_inputMappingEditor && m_inputMappingEditor->HasKeyboardFocus()) &&
                            !(m_postProcessEditor && m_postProcessEditor->HasKeyboardFocus()) &&
                            !(m_foliageEditor && m_foliageEditor->HasKeyboardFocus());
        // The Content Browser owns copy, paste, duplicate, delete and undo for files while focused.
        const bool edit = editor && !(m_contentBrowser && m_contentBrowser->HasKeyboardFocus());
        const bool scene = edit && IsSceneShortcutContext();
        m_commandScopeActive = {global, editor, edit, scene};

        // The foliage brush takes Escape and Delete while painting.
        const bool foliageToolHandledKey = scene && !isRuntimeRunning && HandleFoliageToolKeys();

        const bool editing = !isRuntimeRunning;
        m_commands.Bind("play.play", [this] { m_pendingRuntimeStart = true; }, editing && m_scene);
        m_commands.Bind("play.stop", [this] { StopEditorRuntime(); }, isRuntimeRunning);
        m_commands.Bind("play.pause", [this] { ApplyPlayModeActions({.togglePause = true}); }, isRuntimeRunning);
        m_commands.Bind("play.step", [this] { ApplyPlayModeActions({.step = true}); }, isRuntimeRunning);
        m_commands.Bind("edit.undo", [this] { Undo(); }, editing && CanUndo());
        m_commands.Bind("edit.redo", [this] { Redo(); }, editing && CanRedo());
        const bool hasSelection = GetSelectedEntity() != nullptr;
        m_commands.Bind("scene.copy", [this] { CopySelectedEntity(); }, editing && hasSelection);
        m_commands.Bind("scene.paste", [this] { PasteCopiedEntity(); }, editing && m_scene);
        m_commands.Bind("scene.duplicate", [this] { DuplicateSelectedEntity(); }, editing && hasSelection);
        m_commands.Bind("scene.delete", [this] { DeleteSelectedEntity(); }, editing && hasSelection && !foliageToolHandledKey);
        m_commands.Bind("scene.deselect", [this] { SetSelectedEntity(nullptr); }, editing && hasSelection && !foliageToolHandledKey);
        m_commands.Bind("scene.dropToGround", [this] { m_showGroundPlacement = true; }, editing && hasSelection && !m_activeBakeTask);
        if (m_gameViewportPanel)
            m_commands.Bind("view.gameViewportFullscreen", [this] { m_gameViewportPanel->ToggleFullscreen(); });
        m_commands.Bind("build.scripts", [this] { BuildProjectScripts(); }, m_project && !IsRuntimeExportProject());
        m_commands.Bind("editor.commandPalette", [this] { m_openCommandPalette = true; });
        m_commands.Bind("editor.preferences", [this] { m_showPreferences = true; });
        m_commands.Bind("editor.keyboardShortcuts", [this] { SelectKeyboardPreferencesPage(); m_showPreferences = true; });
        if (profilerPanel)
            m_commands.Bind("profiler.copyMetrics", [profilerPanel] { profilerPanel->CopyMetricsToClipboard(); });

        // Every panel can be brought forward from the palette ("Show Console").
        for (Panel *panel : m_panelManager.GetPanels())
        {
            const auto label = PanelLabel(panel->GetName());
            if (label.empty())
                continue;
            const std::string id = "window." + label;
            m_commands.Register({.id = id, .label = "Show " + label, .category = "Window"});
            m_commands.Bind(id, [panel] { panel->Focus(); });
        }
    }

    void EditorShell::DispatchEditorCommands()
    {
        m_commands.RunQueued();
        m_commands.DispatchShortcuts([this](EditorCommandScope scope) { return m_commandScopeActive[static_cast<std::size_t>(scope)]; });
    }

    void EditorShell::RenderCommandPalette()
    {
        constexpr const char *kPopupId = "##CommandPalette";
        if (m_openCommandPalette)
        {
            ImGui::OpenPopup(kPopupId);
            m_openCommandPalette = false;
            m_commandPaletteQuery.fill('\0');
            m_commandPaletteSelection = 0;
        }

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        const float width = (std::min)(640.0f, viewport->WorkSize.x - 32.0f);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 48.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        if (!ImGui::BeginPopup(kPopupId))
            return;

        // Results: available commands, then project assets once something is typed.
        struct Result
        {
            int score = 0;
            std::string label;
            std::string detail;
            const EditorCommandDefinition *command = nullptr;
            const assets::ProjectAssetEntry *asset = nullptr;
        };
        const std::string query = m_commandPaletteQuery.data();
        std::vector<Result> results;
        for (const auto &command : m_commands.List())
        {
            if (!command.showInPalette || !m_commands.IsAvailable(command.id))
                continue;
            const std::string label = command.category + ": " + command.label;
            const int score = FuzzyMatchScore(label, query);
            if (score >= 0)
                results.push_back({score, label, m_commands.GetShortcutText(command.id), &command, nullptr});
        }
        if (m_project && !query.empty())
        {
            for (const auto &entry : m_project->GetManifest().assetEntries)
            {
                const auto name = std::filesystem::path(entry.reference).filename().string();
                const int score = FuzzyMatchScore(name, query);
                if (score >= 0)
                    results.push_back({score - 50, name, entry.reference, nullptr, &entry});
            }
        }
        std::stable_sort(results.begin(), results.end(), [](const Result &a, const Result &b) { return a.score > b.score; });
        constexpr std::size_t kMaxResults = 60;
        if (results.size() > kMaxResults)
            results.resize(kMaxResults);
        const int resultCount = static_cast<int>(results.size());

        // Up and Down move through the results while typing.
        const auto history = [](ImGuiInputTextCallbackData *data)
        {
            auto *selection = static_cast<int *>(data->UserData);
            *selection += data->EventKey == ImGuiKey_UpArrow ? -1 : 1;
            return 0;
        };
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(width);
        const bool enter = ImGui::InputTextWithHint("##CommandQuery", (std::string(icons::Search) + "  Run a command, or type an asset name to open").c_str(),
                                                    m_commandPaletteQuery.data(), m_commandPaletteQuery.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, history,
                                                    &m_commandPaletteSelection);
        if (ImGui::IsItemEdited())
            m_commandPaletteSelection = 0;
        if (ImGui::IsItemDeactivated() && !enter && ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        m_commandPaletteSelection = resultCount == 0 ? 0 : std::clamp(m_commandPaletteSelection, 0, resultCount - 1);

        const Result *chosen = enter && resultCount > 0 ? &results[static_cast<std::size_t>(m_commandPaletteSelection)] : nullptr;
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const float listHeight = (std::min)(static_cast<float>((std::max)(resultCount, 1)), 14.0f) * rowHeight + ImGui::GetStyle().WindowPadding.y;
        if (ImGui::BeginChild("##CommandResults", ImVec2(width, listHeight)))
        {
            if (resultCount == 0)
                ImGui::TextDisabled(query.empty() ? "No commands available." : "Nothing matches.");
            for (int i = 0; i < resultCount; ++i)
            {
                const auto &result = results[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                const bool selected = i == m_commandPaletteSelection;
                const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
                if (ImGui::Selectable(result.asset ? (std::string(icons::Search) + "  " + result.label).c_str() : result.label.c_str(), selected))
                    chosen = &result;
                if (selected && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)))
                    ImGui::SetScrollHereY();
                if (!result.detail.empty())
                {
                    const float detailWidth = ImGui::CalcTextSize(result.detail.c_str()).x;
                    ImGui::SameLine((std::max)(rowRight - detailWidth, rowRight * 0.5f));
                    ImGui::TextColored(GetEditorColor(EditorColor::TextMuted), "%s", result.detail.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Enter runs  |  Up/Down select  |  Esc closes");

        if (chosen)
        {
            if (chosen->command)
                m_commands.Queue(chosen->command->id); // Runs with next frame's bindings, outside this popup.
            else if (chosen->asset && !OpenProjectAsset(*chosen->asset) && m_contentBrowser)
            {
                m_contentBrowser->RevealAsset(chosen->asset->reference);
                m_contentBrowser->Focus();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool EditorShell::OpenProjectAsset(const assets::ProjectAssetEntry &asset)
    {
        if (!m_project)
            return false;
        using Type = assets::ProjectAssetType;
        const auto path = m_project->ResolveAssetReference(asset.reference);
        const auto extension = path.extension();
        if (extension == ".rml")
            OpenRmlDocument(asset.reference);
        else if (extension == ".plutoscene" || extension == ".plutoprefab")
            OpenSceneFromPath(path);
        else if (extension == ".cs")
            OpenScriptSource(path);
        else if (asset.type == Type::Material)
            OpenMaterialAsset(asset.reference);
        else if (asset.type == Type::Mesh)
            OpenMeshAsset(asset.reference);
        else if (asset.type == Type::ShaderGraph)
            OpenShaderGraphAsset(asset.reference);
        else if (asset.type == Type::AnimationGraph)
            OpenAnimationGraphAsset(asset.reference);
        else if (asset.type == Type::AnimationClip)
            OpenAnimationClipAsset(asset.reference);
        else if (asset.type == Type::ParticleSystem)
            OpenParticleSystemAsset(asset.reference);
        else if (asset.type == Type::LoadingScreen)
            OpenLoadingScreenAsset(asset.reference);
        else if (asset.type == Type::UITheme)
            OpenUIThemeEditor(UIThemeTarget::ForAsset(asset.reference));
        else if (asset.type == Type::InputMapping)
            OpenInputMappingAsset(asset.reference);
        else if (asset.type == Type::PostProcessPreset)
            OpenPostProcessPresetAsset(asset.reference);
        else if (asset.type == Type::FoliageType)
            OpenFoliageTypeAsset(asset.reference);
        else
            return false;
        return true;
    }
}
