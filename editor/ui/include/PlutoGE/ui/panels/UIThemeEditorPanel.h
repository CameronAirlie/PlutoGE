#pragma once

#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/ui/theme/UIThemeDocument.h"
#include "PlutoGE/ui/theme/UIThemeViews.h"

#include <memory>
#include <optional>
#include <string>

namespace PlutoGE::ui
{
    // Visual editor for UI themes: styles (with inheritance and per-state
    // values), shared variables, and the theme as text. Edits preview live in
    // the scene; asset themes save to their file, local styles with the scene.
    class UIThemeEditorPanel : public Panel
    {
    public:
        explicit UIThemeEditorPanel(const PanelConfig &config);
        ~UIThemeEditorPanel() override;

        void Render() override;
        void OnProjectChanged() override;

        // Shows a theme, selecting `style` when given. Unsaved changes to
        // another theme are offered for saving first.
        void Open(const UIThemeTarget &target, const std::string &style = {});
        // Shows an already-open document, e.g. one with custom storage. Replaces
        // the current document without asking.
        void Open(std::unique_ptr<UIThemeDocument> document);
        [[nodiscard]] bool HasUnsavedChanges() const;
        [[nodiscard]] const UIThemeDocument *GetDocument() const { return m_document.get(); }

    private:
        enum class Tab
        {
            Styles,
            Variables,
            Source,
        };

        void OpenNow(const UIThemeTarget &target, const std::string &style);
        void DrawEmptyState();
        void DrawToolbar();
        void DrawBaseThemes();
        void DrawProblems();
        void DrawSavePrompt();
        void HandleShortcuts();

        std::unique_ptr<UIThemeDocument> m_document;
        std::optional<std::pair<UIThemeTarget, std::string>> m_pendingOpen;
        bool m_openSavePrompt = false;
        std::optional<Tab> m_requestedTab;
        theme::StylesViewState m_styles;
        theme::VariablesViewState m_variables;
        theme::SourceViewState m_source;
        std::string m_pickerReference;
        std::string m_baseThemePicker;
        std::string m_error;
    };
}
