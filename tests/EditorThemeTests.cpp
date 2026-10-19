#include "PlutoGE/ui/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE::ui;

    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    float Linear(float channel)
    {
        return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
    }

    // WCAG contrast ratio between two opaque colours.
    float Contrast(const ImVec4 &a, const ImVec4 &b)
    {
        const auto luminance = [](const ImVec4 &c)
        { return 0.2126f * Linear(c.x) + 0.7152f * Linear(c.y) + 0.0722f * Linear(c.z); };
        const float la = luminance(a);
        const float lb = luminance(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
    }

    void SetSettingsDirectory(const std::filesystem::path &directory)
    {
#ifdef _WIN32
        _putenv_s("APPDATA", directory.string().c_str());
#else
        setenv("XDG_CONFIG_HOME", directory.string().c_str(), 1);
#endif
    }

    void BuiltinThemesStayReadable()
    {
        for (const auto &theme : GetBuiltinEditorThemes())
        {
            const auto &p = theme.palette;
            Require(Contrast(p.text, p.panel) >= 7.0f, theme.name + ": primary text must be highly readable on panels");
            Require(Contrast(p.textMuted, p.panel) >= 3.0f, theme.name + ": secondary text must stay legible on panels");
            Require(Contrast(p.text, p.surface) >= 4.5f, theme.name + ": field text must be readable");
            // Radio buttons draw CheckMark over FrameBg; checkboxes over CheckboxSelectedBg.
            const ImVec4 check = DeriveEditorThemeColor(theme, ImGuiCol_CheckMark);
            Require(Contrast(check, DeriveEditorThemeColor(theme, ImGuiCol_FrameBg)) >= 2.0f,
                    theme.name + ": radio button dots must be visible");
            Require(Contrast(check, DeriveEditorThemeColor(theme, ImGuiCol_CheckboxSelectedBg)) >= 2.0f,
                    theme.name + ": checkbox marks must be visible");
        }
    }

    void SerializationRoundTrips()
    {
        EditorTheme theme = GetBuiltinEditorThemes()[1];
        theme.name = "Studio";
        theme.light = true;
        theme.metrics.cornerRadius = 9.0f;
        theme.metrics.density = EditorDensity::Compact;
        theme.metrics.treeLines = false;
        theme.overrides[ImGuiCol_PlotLines] = ImVec4(1.0f, 0.0f, 0.5f, 0.5f);

        EditorTheme parsed;
        Require(DeserializeEditorTheme(SerializeEditorTheme(theme), parsed), "Serialised themes must parse");
        Require(parsed.name == "Studio", "Theme names round-trip");
        Require(EditorThemesEqual(theme, parsed), "Palette, metrics and overrides round-trip");
        Require(parsed.overrides.size() == 1 && parsed.overrides.contains(ImGuiCol_PlotLines), "Overrides keep their colour slot");

        EditorTheme rejected;
        Require(!DeserializeEditorTheme("not a theme", rejected), "Text without theme keys is rejected");

        EditorTheme partial;
        Require(DeserializeEditorTheme("Name=Partial\nAccent=#FF0000\nUnknownKey=1\n", partial), "Partial themes load");
        Require(partial.palette.accent.x == 1.0f && partial.palette.accent.y == 0.0f, "Six-digit colours parse as opaque RGB");
        Require(partial.palette.panel.x == GetBuiltinEditorThemes().front().palette.panel.x,
                "Missing keys fall back to the default theme");
    }

    void ApplyingThemeUpdatesStyle()
    {
        SelectEditorTheme(GetBuiltinEditorThemes().front());
        auto &active = GetActiveEditorTheme();
        active.palette.accent = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        active.overrides[ImGuiCol_WindowBg] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        active.metrics.cornerRadius = 0.0f;
        ApplyActiveEditorTheme();
        const ImGuiStyle &style = ImGui::GetStyle();
        Require(style.Colors[ImGuiCol_SliderGrab].x == 1.0f && style.Colors[ImGuiCol_SliderGrab].y == 0.0f,
                "Accent drives derived ImGui colours");
        Require(style.Colors[ImGuiCol_WindowBg].y == 1.0f, "Overrides win over derived colours");
        Require(style.FrameRounding == 0.0f, "Metrics drive style sizes");
        Require(IsActiveEditorThemeModified(), "Edits mark the active theme modified");
    }

    void SettingsPersist()
    {
        SelectEditorTheme(GetBuiltinEditorThemes()[2]);
        GetActiveEditorTheme().metrics.scrollbarSize = 14.0f;
        SaveActiveThemeAsCustom("My Graphite");
        SaveActiveThemeAsCustom(GetBuiltinEditorThemes().front().name);
        Require(GetActiveEditorThemeSource() == "My Graphite", "Built-in names cannot be overwritten");
        GetActiveEditorTheme().metrics.cornerRadius = 11.0f;
        SaveEditorThemeSettings();
        Require(std::filesystem::exists(GetEditorThemeSettingsPath()), "Settings are written to the user settings directory");

        SelectEditorTheme(GetBuiltinEditorThemes().front());
        SaveEditorThemeSettings();
        LoadEditorThemeSettings();
        Require(GetActiveEditorThemeSource() == GetBuiltinEditorThemes().front().name, "Active theme source persists");
        Require(GetCustomEditorThemes().size() == 1 && GetCustomEditorThemes()[0].name == "My Graphite" &&
                    GetCustomEditorThemes()[0].metrics.scrollbarSize == 14.0f,
                "Custom themes persist");

        SelectEditorTheme(GetCustomEditorThemes()[0]);
        GetActiveEditorTheme().metrics.cornerRadius = 11.0f;
        SaveEditorThemeSettings();
        LoadEditorThemeSettings();
        Require(GetActiveEditorThemeSource() == "My Graphite" && GetActiveEditorTheme().metrics.cornerRadius == 11.0f,
                "Unsaved edits to the active theme persist");
        Require(IsActiveEditorThemeModified(), "Edits stay marked modified after reload");

        DeleteCustomEditorTheme("My Graphite");
        Require(GetCustomEditorThemes().empty(), "Custom themes can be deleted");
    }
}

int main()
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    const auto directory = std::filesystem::temp_directory_path() / "PlutoGE-theme-tests";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    // Selecting a theme saves it, so redirect before any test touches the user's settings.
    SetSettingsDirectory(directory);
    int result = 0;
    try
    {
        BuiltinThemesStayReadable();
        SerializationRoundTrips();
        ApplyingThemeUpdatesStyle();
        SettingsPersist();
        std::cout << "Editor theme tests passed\n";
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(directory, error);
    ImGui::DestroyContext();
    return result;
}
