#pragma once

#include "PlutoGE/scene/ui/UITheme.h"

#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    class UIThemeDocument;
}

// The tabs of the theme editor. Each view keeps its own UI state and edits
// the document through UIThemeDocument::Modify, so every change is undoable.
namespace PlutoGE::ui::theme
{
    struct StylesViewState
    {
        std::string selected;
        scene::UIStyleState state = scene::UIStyleState::Normal;
        std::string search;
        std::string nameBuffer;      // new-style and rename popups
        std::string newStyleBase;
        std::string renaming;        // style being renamed
        bool openNewStylePopup = false;
        bool openRenamePopup = false;
    };
    void DrawStylesView(UIThemeDocument &document, StylesViewState &state);

    struct VariablesViewState
    {
        std::string nameBuffer;
        scene::UIThemeVariableType newType = scene::UIThemeVariableType::Color;
        std::string editingName; // row whose name field is focused
        std::string editingBuffer;
    };
    void DrawVariablesView(UIThemeDocument &document, VariablesViewState &state);

    struct SourceViewState
    {
        std::string text;
        std::uint64_t revision = 0;
        std::vector<std::string> errors;
        bool typing = false; // the text box had focus last frame
    };
    void DrawSourceView(UIThemeDocument &document, SourceViewState &state);

    // Elements in the open scene using a style, and renaming it on them.
    [[nodiscard]] std::size_t CountStyleUsersInScene(const std::string &style);
    void RenameStyleInScene(const std::string &from, const std::string &to);
}
