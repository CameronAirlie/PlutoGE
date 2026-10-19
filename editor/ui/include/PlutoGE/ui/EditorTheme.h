#pragma once

#include <imgui.h>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    // A theme is a small semantic palette plus a few layout metrics. Every
    // ImGuiCol is derived from these, so a handful of choices restyles the
    // whole editor; Overrides then pin individual ImGui colours if needed.
    struct EditorThemePalette
    {
        ImVec4 background;  // Window chrome, menu bar, empty dock space.
        ImVec4 panel;       // Panel bodies.
        ImVec4 surface;     // Input fields and other recessed controls.
        ImVec4 elevated;    // Popups, menus and tooltips.
        ImVec4 border;
        ImVec4 text;
        ImVec4 textMuted;
        ImVec4 accent;
        ImVec4 success;
        ImVec4 warning;
        ImVec4 error;
    };

    enum class EditorDensity
    {
        Compact,
        Default,
        Comfortable,
    };

    struct EditorThemeMetrics
    {
        float cornerRadius = 5.0f;
        EditorDensity density = EditorDensity::Default;
        float scrollbarSize = 10.0f;
        bool frameBorders = true;
        bool treeLines = true;
        bool tabOverline = true;
        bool windowMenuButton = false;
    };

    struct EditorTheme
    {
        std::string name;
        bool light = false;
        EditorThemePalette palette{};
        EditorThemeMetrics metrics{};
        std::map<ImGuiCol, ImVec4> overrides;
    };

    // Semantic colours for panels that draw their own widgets.
    enum class EditorColor
    {
        Background,
        Panel,
        Surface,
        Elevated,
        Border,
        Text,
        TextMuted,
        Accent,
        AccentHovered,
        AccentText, // Readable text drawn on top of the accent colour.
        Selection,
        SelectionHovered,
        Success,
        Warning,
        Error,
    };

    [[nodiscard]] const std::vector<EditorTheme> &GetBuiltinEditorThemes();
    [[nodiscard]] const std::vector<EditorTheme> &GetCustomEditorThemes();
    [[nodiscard]] EditorTheme &GetActiveEditorTheme();
    // Theme the active one was loaded from; used to show unsaved edits.
    [[nodiscard]] const std::string &GetActiveEditorThemeSource();
    [[nodiscard]] bool IsActiveEditorThemeModified();
    [[nodiscard]] const EditorTheme *FindEditorTheme(std::string_view name);
    [[nodiscard]] bool IsBuiltinEditorTheme(std::string_view name);

    // Makes `theme` active and applies it to the current ImGui context.
    void SelectEditorTheme(const EditorTheme &theme);
    // Re-derives the ImGui style after the active theme was edited in place.
    void ApplyActiveEditorTheme();
    void SaveActiveThemeAsCustom(const std::string &name);
    void DeleteCustomEditorTheme(std::string_view name);

    [[nodiscard]] ImVec4 GetEditorColor(EditorColor color);
    [[nodiscard]] ImU32 GetEditorColorU32(EditorColor color, float alphaMultiplier = 1.0f);
    [[nodiscard]] ImVec4 DeriveEditorThemeColor(const EditorTheme &theme, ImGuiCol color);

    [[nodiscard]] std::string SerializeEditorTheme(const EditorTheme &theme);
    [[nodiscard]] bool DeserializeEditorTheme(std::string_view text, EditorTheme &theme);
    [[nodiscard]] bool EditorThemesEqual(const EditorTheme &a, const EditorTheme &b);

    // Theme preferences live in the user's settings directory, not the project.
    [[nodiscard]] std::filesystem::path GetEditorThemeSettingsPath();
    void LoadEditorThemeSettings();
    void SaveEditorThemeSettings();
}
