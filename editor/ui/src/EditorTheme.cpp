#include "PlutoGE/ui/EditorTheme.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kDefaultThemeName = "Pluto Dark";

        ImVec4 Hex(unsigned int rgb, float alpha = 1.0f)
        {
            return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
        }

        ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        ImVec4 WithAlpha(ImVec4 color, float alpha)
        {
            color.w = alpha;
            return color;
        }

        float Luminance(const ImVec4 &color)
        {
            return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
        }

        float DensityScale(EditorDensity density)
        {
            switch (density)
            {
            case EditorDensity::Compact: return 0.72f;
            case EditorDensity::Comfortable: return 1.3f;
            default: return 1.0f;
            }
        }

        EditorTheme MakeTheme(const char *name, bool light, const EditorThemePalette &palette,
                              EditorThemeMetrics metrics = {})
        {
            EditorTheme theme;
            theme.name = name;
            theme.light = light;
            theme.palette = palette;
            theme.metrics = metrics;
            return theme;
        }

        std::vector<EditorTheme> BuildBuiltinThemes()
        {
            std::vector<EditorTheme> themes;
            themes.push_back(MakeTheme(kDefaultThemeName, false, {
                Hex(0x16171B), Hex(0x1E1F24), Hex(0x26282E), Hex(0x2A2C33), Hex(0x34363E),
                Hex(0xE4E6EB), Hex(0x8C909A), Hex(0x5B8CFF), Hex(0x4CC38A), Hex(0xF0B43C), Hex(0xF0605A)}));
            themes.push_back(MakeTheme("Midnight", false, {
                Hex(0x0B0E14), Hex(0x11151D), Hex(0x181D28), Hex(0x1C2230), Hex(0x252C3A),
                Hex(0xDCE3F0), Hex(0x7D889D), Hex(0x8B7BFF), Hex(0x3FCF8E), Hex(0xF5B946), Hex(0xFF6B6B)},
                {.cornerRadius = 7.0f}));
            themes.push_back(MakeTheme("Graphite", false, {
                Hex(0x1B1B1B), Hex(0x252525), Hex(0x2F2F2F), Hex(0x323232), Hex(0x3B3B3B),
                Hex(0xE8E8E8), Hex(0x979797), Hex(0xF29D38), Hex(0x6CC570), Hex(0xF2C038), Hex(0xEB5E55)},
                {.cornerRadius = 3.0f, .density = EditorDensity::Compact}));
            themes.push_back(MakeTheme("Nord", false, {
                Hex(0x242933), Hex(0x2E3440), Hex(0x3B4252), Hex(0x353C4A), Hex(0x434C5E),
                Hex(0xECEFF4), Hex(0x9AA4B6), Hex(0x88C0D0), Hex(0xA3BE8C), Hex(0xEBCB8B), Hex(0xBF616A)},
                {.cornerRadius = 6.0f}));
            themes.push_back(MakeTheme("Pluto Classic", false, {
                Hex(0x121417), Hex(0x1B1D20), Hex(0x292D30), Hex(0x1F2125), Hex(0x3C4249),
                Hex(0xE0E8F0), Hex(0x7A8794), Hex(0x63B8C2), Hex(0x4CC38A), Hex(0xF0B43C), Hex(0xF0605A)},
                {.cornerRadius = 4.0f, .frameBorders = false, .tabOverline = false}));
            themes.push_back(MakeTheme("Daylight", true, {
                Hex(0xE4E7EC), Hex(0xF5F6F8), Hex(0xFFFFFF), Hex(0xFFFFFF), Hex(0xD3D7DE),
                Hex(0x1E2329), Hex(0x6A717D), Hex(0x2F6FEB), Hex(0x1F9D63), Hex(0xC98A0B), Hex(0xD93D3D)}));
            themes.push_back(MakeTheme("High Contrast", false, {
                Hex(0x000000), Hex(0x0B0B0B), Hex(0x161616), Hex(0x161616), Hex(0x7A7A7A),
                Hex(0xFFFFFF), Hex(0xC2C2C2), Hex(0xFFD43B), Hex(0x5BE37D), Hex(0xFFB020), Hex(0xFF5A5A)},
                {.cornerRadius = 2.0f}));
            return themes;
        }

        struct ThemeState
        {
            EditorTheme active;
            std::string source;
            std::vector<EditorTheme> custom;
        };

        ThemeState &State()
        {
            static ThemeState state = []
            {
                ThemeState initial;
                initial.active = GetBuiltinEditorThemes().front();
                initial.source = initial.active.name;
                return initial;
            }();
            return state;
        }

        ImVec4 SemanticColor(const EditorTheme &theme, EditorColor color)
        {
            const auto &p = theme.palette;
            switch (color)
            {
            case EditorColor::Background: return p.background;
            case EditorColor::Panel: return p.panel;
            case EditorColor::Surface: return p.surface;
            case EditorColor::Elevated: return p.elevated;
            case EditorColor::Border: return p.border;
            case EditorColor::Text: return p.text;
            case EditorColor::TextMuted: return p.textMuted;
            case EditorColor::Accent: return p.accent;
            case EditorColor::AccentHovered: return Mix(p.accent, theme.light ? Hex(0x000000) : Hex(0xFFFFFF), 0.15f);
            case EditorColor::AccentText: return Luminance(p.accent) > 0.6f ? Hex(0x111216) : Hex(0xFFFFFF);
            case EditorColor::Selection: return WithAlpha(p.accent, theme.light ? 0.22f : 0.36f);
            case EditorColor::SelectionHovered: return WithAlpha(p.accent, theme.light ? 0.30f : 0.46f);
            case EditorColor::Success: return p.success;
            case EditorColor::Warning: return p.warning;
            case EditorColor::Error: return p.error;
            }
            return p.text;
        }

        bool AccentFillsCheckboxes(const EditorTheme &theme)
        {
            // Radio buttons draw the check colour on FrameBg, so a white check
            // only works while the surface stays dark.
            return !theme.light && Luminance(theme.palette.accent) <= 0.6f;
        }

        void DeriveColors(const EditorTheme &theme, ImVec4 *colors)
        {
            const auto &p = theme.palette;
            const auto tint = [&](const ImVec4 &base, float amount) { return Mix(base, p.text, amount); };
            const ImVec4 clear = WithAlpha(p.panel, 0.0f);
            const ImVec4 accentText = SemanticColor(theme, EditorColor::AccentText);
            const ImVec4 accentHover = SemanticColor(theme, EditorColor::AccentHovered);

            colors[ImGuiCol_Text] = p.text;
            colors[ImGuiCol_TextDisabled] = p.textMuted;
            colors[ImGuiCol_WindowBg] = p.panel;
            colors[ImGuiCol_ChildBg] = clear;
            colors[ImGuiCol_PopupBg] = p.elevated;
            colors[ImGuiCol_Border] = p.border;
            colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            colors[ImGuiCol_FrameBg] = p.surface;
            colors[ImGuiCol_FrameBgHovered] = tint(p.surface, 0.05f);
            colors[ImGuiCol_FrameBgActive] = Mix(p.surface, p.accent, 0.14f);
            colors[ImGuiCol_TitleBg] = p.background;
            colors[ImGuiCol_TitleBgActive] = p.background;
            colors[ImGuiCol_TitleBgCollapsed] = p.background;
            colors[ImGuiCol_MenuBarBg] = p.background;
            colors[ImGuiCol_ScrollbarBg] = clear;
            colors[ImGuiCol_ScrollbarGrab] = tint(p.panel, 0.16f);
            colors[ImGuiCol_ScrollbarGrabHovered] = tint(p.panel, 0.28f);
            colors[ImGuiCol_ScrollbarGrabActive] = p.accent;
            if (AccentFillsCheckboxes(theme))
            {
                colors[ImGuiCol_CheckMark] = accentText;
                colors[ImGuiCol_CheckboxSelectedBg] = p.accent;
            }
            else
            {
                colors[ImGuiCol_CheckMark] = p.accent;
                colors[ImGuiCol_CheckboxSelectedBg] = Mix(p.surface, p.accent, 0.16f);
            }
            colors[ImGuiCol_SliderGrab] = p.accent;
            colors[ImGuiCol_SliderGrabActive] = accentHover;
            colors[ImGuiCol_Button] = tint(p.panel, 0.07f);
            colors[ImGuiCol_ButtonHovered] = tint(p.panel, 0.13f);
            colors[ImGuiCol_ButtonActive] = Mix(tint(p.panel, 0.13f), p.accent, 0.35f);
            colors[ImGuiCol_Header] = tint(p.panel, 0.06f);
            colors[ImGuiCol_HeaderHovered] = tint(p.panel, 0.10f);
            colors[ImGuiCol_HeaderActive] = Mix(p.panel, p.accent, 0.35f);
            colors[ImGuiCol_Separator] = p.border;
            colors[ImGuiCol_SeparatorHovered] = WithAlpha(p.accent, 0.70f);
            colors[ImGuiCol_SeparatorActive] = p.accent;
            colors[ImGuiCol_ResizeGrip] = WithAlpha(p.accent, 0.0f);
            colors[ImGuiCol_ResizeGripHovered] = WithAlpha(p.accent, 0.45f);
            colors[ImGuiCol_ResizeGripActive] = p.accent;
            colors[ImGuiCol_InputTextCursor] = p.text;
            colors[ImGuiCol_Tab] = p.background;
            colors[ImGuiCol_TabHovered] = tint(p.panel, 0.08f);
            colors[ImGuiCol_TabSelected] = p.panel;
            colors[ImGuiCol_TabSelectedOverline] = p.accent;
            colors[ImGuiCol_TabDimmed] = p.background;
            colors[ImGuiCol_TabDimmedSelected] = p.panel;
            colors[ImGuiCol_TabDimmedSelectedOverline] = tint(p.panel, 0.22f);
            colors[ImGuiCol_DockingPreview] = WithAlpha(p.accent, 0.35f);
            colors[ImGuiCol_DockingEmptyBg] = p.background;
            colors[ImGuiCol_PlotLines] = p.textMuted;
            colors[ImGuiCol_PlotLinesHovered] = p.accent;
            colors[ImGuiCol_PlotHistogram] = p.accent;
            colors[ImGuiCol_PlotHistogramHovered] = accentHover;
            colors[ImGuiCol_TableHeaderBg] = tint(p.panel, 0.05f);
            colors[ImGuiCol_TableBorderStrong] = p.border;
            colors[ImGuiCol_TableBorderLight] = Mix(p.panel, p.border, 0.6f);
            colors[ImGuiCol_TableRowBg] = clear;
            colors[ImGuiCol_TableRowBgAlt] = WithAlpha(p.text, 0.025f);
            colors[ImGuiCol_TextLink] = theme.light ? p.accent : Mix(p.accent, Hex(0xFFFFFF), 0.25f);
            colors[ImGuiCol_TextSelectedBg] = WithAlpha(p.accent, 0.35f);
            colors[ImGuiCol_TreeLines] = p.border;
            colors[ImGuiCol_DragDropTarget] = p.accent;
            colors[ImGuiCol_DragDropTargetBg] = WithAlpha(p.accent, 0.12f);
            colors[ImGuiCol_UnsavedMarker] = p.warning;
            colors[ImGuiCol_NavCursor] = p.accent;
            colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(p.text, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, theme.light ? 0.20f : 0.40f);
            colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, theme.light ? 0.25f : 0.55f);
        }

        void ApplyTheme(const EditorTheme &theme)
        {
            if (ImGui::GetCurrentContext() == nullptr)
                return;

            ImGuiStyle &style = ImGui::GetStyle();
            DeriveColors(theme, style.Colors);
            for (const auto &[color, value] : theme.overrides)
                if (color >= 0 && color < ImGuiCol_COUNT)
                    style.Colors[color] = value;

            const auto &m = theme.metrics;
            const float d = DensityScale(m.density);
            const float r = std::clamp(m.cornerRadius, 0.0f, 12.0f);

            // Whole-pixel spacing keeps text on the pixel grid at every density.
            const auto spacing = [d](float x, float y) { return ImVec2(std::round(x * d), std::round(y * d)); };
            style.WindowPadding = spacing(10.0f, 8.0f);
            style.FramePadding = spacing(8.0f, 5.0f);
            style.CellPadding = spacing(6.0f, 4.0f);
            style.ItemSpacing = spacing(8.0f, 6.0f);
            style.ItemInnerSpacing = spacing(6.0f, 4.0f);
            style.IndentSpacing = 16.0f;
            style.ScrollbarSize = std::clamp(m.scrollbarSize, 6.0f, 18.0f);
            style.ScrollbarPadding = 2.0f;
            style.GrabMinSize = 8.0f;
            style.SeparatorTextPadding = ImVec2(16.0f, style.FramePadding.y);

            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.FrameBorderSize = m.frameBorders ? 1.0f : 0.0f;
            style.TabBorderSize = 0.0f;
            style.TabBarBorderSize = 1.0f;
            style.TabBarOverlineSize = m.tabOverline ? 2.0f : 0.0f;
            style.SeparatorTextBorderSize = 1.0f;
            style.DockingSeparatorSize = 2.0f;

            style.WindowRounding = r;
            style.ChildRounding = r;
            style.FrameRounding = r * 0.8f;
            style.PopupRounding = r > 0.0f ? r + 2.0f : 0.0f;
            style.ScrollbarRounding = r > 0.0f ? 12.0f : 0.0f;
            style.GrabRounding = r * 0.8f;
            style.TabRounding = r * 0.8f;
            style.MenuItemRounding = r * 0.6f;
            style.DragDropTargetRounding = -1.0f;
            style.DragDropTargetBorderSize = 2.0f;

            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = m.windowMenuButton ? ImGuiDir_Left : ImGuiDir_None;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.TabCloseButtonMinWidthSelected = -1.0f;
            style.TabCloseButtonMinWidthUnselected = 0.0f;
            style.TreeLinesFlags = m.treeLines ? ImGuiTreeNodeFlags_DrawLinesToNodes : ImGuiTreeNodeFlags_DrawLinesNone;
            style.TreeLinesSize = 1.0f;
            style.TreeLinesRounding = r > 0.0f ? 4.0f : 0.0f;
            style.DisabledAlpha = 0.5f;
            style.CircleTessellationMaxError = 0.2f;

            // Platform windows are composited opaque; transparency would show the desktop.
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        std::filesystem::path GetSettingsDirectory()
        {
#ifdef _WIN32
            if (const char *appData = std::getenv("APPDATA"); appData != nullptr && appData[0] != '\0')
                return std::filesystem::path(appData) / "PlutoGE";
#else
            if (const char *configHome = std::getenv("XDG_CONFIG_HOME"); configHome != nullptr && configHome[0] != '\0')
                return std::filesystem::path(configHome) / "PlutoGE";
            if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
                return std::filesystem::path(home) / ".config" / "PlutoGE";
#endif
            return std::filesystem::current_path();
        }

        std::string FormatColor(const ImVec4 &color)
        {
            const auto channel = [](float value) { return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", channel(color.x), channel(color.y),
                          channel(color.z), channel(color.w));
            return buffer;
        }

        bool ParseColor(std::string_view text, ImVec4 &color)
        {
            if (!text.empty() && text.front() == '#')
                text.remove_prefix(1);
            if (text.size() != 6 && text.size() != 8)
                return false;
            const std::string digits(text);
            char *end = nullptr;
            const unsigned long value = std::strtoul(digits.c_str(), &end, 16);
            if (end != digits.c_str() + digits.size())
                return false;
            const unsigned int rgba = digits.size() == 6 ? static_cast<unsigned int>(value << 8 | 0xFF)
                                                         : static_cast<unsigned int>(value);
            color = ImVec4(static_cast<float>((rgba >> 24) & 0xFF) / 255.0f, static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
                           static_cast<float>((rgba >> 8) & 0xFF) / 255.0f, static_cast<float>(rgba & 0xFF) / 255.0f);
            return true;
        }

        struct PaletteField
        {
            const char *key;
            ImVec4 EditorThemePalette::*member;
        };

        constexpr std::array<PaletteField, 11> kPaletteFields = {{
            {"Background", &EditorThemePalette::background},
            {"Panel", &EditorThemePalette::panel},
            {"Surface", &EditorThemePalette::surface},
            {"Elevated", &EditorThemePalette::elevated},
            {"Border", &EditorThemePalette::border},
            {"Text", &EditorThemePalette::text},
            {"TextMuted", &EditorThemePalette::textMuted},
            {"Accent", &EditorThemePalette::accent},
            {"Success", &EditorThemePalette::success},
            {"Warning", &EditorThemePalette::warning},
            {"Error", &EditorThemePalette::error},
        }};

        constexpr std::array<const char *, 3> kDensityNames = {"Compact", "Default", "Comfortable"};

        bool ColorsEqual(const ImVec4 &a, const ImVec4 &b)
        {
            return FormatColor(a) == FormatColor(b);
        }

        // Applies one `Key=Value` line. Unknown keys are ignored so newer files load.
        bool ApplyThemeLine(std::string_view line, EditorTheme &theme)
        {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos)
                return false;
            const std::string_view key = line.substr(0, separator);
            const std::string_view value = line.substr(separator + 1);
            const std::string valueText(value);
            const auto flag = [&] { return value == "1" || value == "true"; };

            if (key == "Name") { theme.name = valueText; return true; }
            if (key == "Light") { theme.light = flag(); return true; }
            if (key == "CornerRadius") { theme.metrics.cornerRadius = std::strtof(valueText.c_str(), nullptr); return true; }
            if (key == "ScrollbarSize") { theme.metrics.scrollbarSize = std::strtof(valueText.c_str(), nullptr); return true; }
            if (key == "FrameBorders") { theme.metrics.frameBorders = flag(); return true; }
            if (key == "TreeLines") { theme.metrics.treeLines = flag(); return true; }
            if (key == "TabOverline") { theme.metrics.tabOverline = flag(); return true; }
            if (key == "WindowMenuButton") { theme.metrics.windowMenuButton = flag(); return true; }
            if (key == "Density")
            {
                for (std::size_t i = 0; i < kDensityNames.size(); ++i)
                    if (value == kDensityNames[i])
                        theme.metrics.density = static_cast<EditorDensity>(i);
                return true;
            }
            for (const auto &field : kPaletteFields)
                if (key == field.key)
                    return ParseColor(value, theme.palette.*field.member);
            constexpr std::string_view overridePrefix = "Override.";
            if (key.substr(0, overridePrefix.size()) == overridePrefix)
            {
                const std::string_view colorName = key.substr(overridePrefix.size());
                ImVec4 color;
                for (int i = 0; i < ImGuiCol_COUNT; ++i)
                    if (colorName == ImGui::GetStyleColorName(i) && ParseColor(value, color))
                    {
                        theme.overrides[i] = color;
                        return true;
                    }
            }
            return false;
        }

        std::vector<std::string_view> SplitLines(std::string_view text)
        {
            std::vector<std::string_view> lines;
            while (!text.empty())
            {
                const auto end = text.find('\n');
                std::string_view line = text.substr(0, end);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.remove_suffix(1);
                lines.push_back(line);
                if (end == std::string_view::npos)
                    break;
                text.remove_prefix(end + 1);
            }
            return lines;
        }
    }

    const std::vector<EditorTheme> &GetBuiltinEditorThemes()
    {
        static const std::vector<EditorTheme> themes = BuildBuiltinThemes();
        return themes;
    }

    const std::vector<EditorTheme> &GetCustomEditorThemes() { return State().custom; }
    EditorTheme &GetActiveEditorTheme() { return State().active; }
    const std::string &GetActiveEditorThemeSource() { return State().source; }

    const EditorTheme *FindEditorTheme(std::string_view name)
    {
        for (const auto &theme : State().custom)
            if (theme.name == name)
                return &theme;
        for (const auto &theme : GetBuiltinEditorThemes())
            if (theme.name == name)
                return &theme;
        return nullptr;
    }

    bool IsBuiltinEditorTheme(std::string_view name)
    {
        return std::any_of(GetBuiltinEditorThemes().begin(), GetBuiltinEditorThemes().end(),
                           [&](const EditorTheme &theme) { return theme.name == name; });
    }

    bool IsActiveEditorThemeModified()
    {
        const EditorTheme *source = FindEditorTheme(State().source);
        return source == nullptr || !EditorThemesEqual(*source, State().active);
    }

    bool EditorThemesEqual(const EditorTheme &a, const EditorTheme &b)
    {
        // Compare through the serialised form so float noise below one colour
        // step does not count as an edit. Names are deliberately ignored.
        EditorTheme left = a;
        EditorTheme right = b;
        left.name.clear();
        right.name.clear();
        return SerializeEditorTheme(left) == SerializeEditorTheme(right);
    }

    void SelectEditorTheme(const EditorTheme &theme)
    {
        State().active = theme;
        State().source = theme.name;
        ApplyTheme(State().active);
        SaveEditorThemeSettings();
    }

    void ApplyActiveEditorTheme() { ApplyTheme(State().active); }

    void SaveActiveThemeAsCustom(const std::string &name)
    {
        if (name.empty() || IsBuiltinEditorTheme(name))
            return;
        auto &state = State();
        state.active.name = name;
        state.source = name;
        auto existing = std::find_if(state.custom.begin(), state.custom.end(),
                                     [&](const EditorTheme &theme) { return theme.name == name; });
        if (existing != state.custom.end())
            *existing = state.active;
        else
            state.custom.push_back(state.active);
        SaveEditorThemeSettings();
    }

    void DeleteCustomEditorTheme(std::string_view name)
    {
        auto &state = State();
        std::erase_if(state.custom, [&](const EditorTheme &theme) { return theme.name == name; });
        if (state.source == name)
        {
            // The active copy survives deletion until the user picks another theme.
            state.source = state.active.name = kDefaultThemeName;
        }
        SaveEditorThemeSettings();
    }

    ImVec4 GetEditorColor(EditorColor color)
    {
        return SemanticColor(State().active, color);
    }

    ImU32 GetEditorColorU32(EditorColor color, float alphaMultiplier)
    {
        ImVec4 value = GetEditorColor(color);
        value.w *= alphaMultiplier;
        return ImGui::ColorConvertFloat4ToU32(value);
    }

    ImVec4 DeriveEditorThemeColor(const EditorTheme &theme, ImGuiCol color)
    {
        std::array<ImVec4, ImGuiCol_COUNT> colors{};
        DeriveColors(theme, colors.data());
        return color >= 0 && color < ImGuiCol_COUNT ? colors[color] : ImVec4();
    }

    std::string SerializeEditorTheme(const EditorTheme &theme)
    {
        std::ostringstream out;
        out << "Name=" << theme.name << '\n';
        out << "Light=" << (theme.light ? 1 : 0) << '\n';
        for (const auto &field : kPaletteFields)
            out << field.key << '=' << FormatColor(theme.palette.*field.member) << '\n';
        const auto &m = theme.metrics;
        char number[32];
        std::snprintf(number, sizeof(number), "%.1f", m.cornerRadius);
        out << "CornerRadius=" << number << '\n';
        out << "Density=" << kDensityNames[static_cast<std::size_t>(m.density)] << '\n';
        std::snprintf(number, sizeof(number), "%.1f", m.scrollbarSize);
        out << "ScrollbarSize=" << number << '\n';
        out << "FrameBorders=" << (m.frameBorders ? 1 : 0) << '\n';
        out << "TreeLines=" << (m.treeLines ? 1 : 0) << '\n';
        out << "TabOverline=" << (m.tabOverline ? 1 : 0) << '\n';
        out << "WindowMenuButton=" << (m.windowMenuButton ? 1 : 0) << '\n';
        for (const auto &[color, value] : theme.overrides)
            out << "Override." << ImGui::GetStyleColorName(color) << '=' << FormatColor(value) << '\n';
        return out.str();
    }

    bool DeserializeEditorTheme(std::string_view text, EditorTheme &theme)
    {
        EditorTheme parsed = GetBuiltinEditorThemes().front();
        parsed.name.clear();
        int applied = 0;
        for (const auto line : SplitLines(text))
            if (!line.empty() && line.front() != '[' && line.front() != ';' && ApplyThemeLine(line, parsed))
                ++applied;
        if (applied == 0)
            return false;
        theme = std::move(parsed);
        return true;
    }

    std::filesystem::path GetEditorThemeSettingsPath()
    {
        return GetSettingsDirectory() / "editor-theme.ini";
    }

    void LoadEditorThemeSettings()
    {
        auto &state = State();
        std::ifstream input(GetEditorThemeSettingsPath());
        if (input)
        {
            const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            // Blocks start with [Active] or [Custom]; each holds one serialised theme.
            std::string section;
            std::string block;
            std::string source;
            const auto flush = [&]
            {
                EditorTheme theme;
                if (!section.empty() && DeserializeEditorTheme(block, theme))
                {
                    if (section == "Active")
                        state.active = std::move(theme);
                    else if (section == "Custom" && !theme.name.empty() && !IsBuiltinEditorTheme(theme.name))
                        state.custom.push_back(std::move(theme));
                }
                block.clear();
            };
            state.custom.clear();
            for (const auto line : SplitLines(contents))
            {
                if (!line.empty() && line.front() == '[')
                {
                    flush();
                    section = std::string(line.substr(1, line.find(']') - 1));
                    continue;
                }
                if (section == "Active" && line.substr(0, 7) == "Source=")
                    source = std::string(line.substr(7));
                else
                    block.append(line).push_back('\n');
            }
            flush();
            state.source = source.empty() ? state.active.name : source;
        }
        ApplyTheme(state.active);
    }

    void SaveEditorThemeSettings()
    {
        const auto &state = State();
        const auto path = GetEditorThemeSettingsPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
            return;
        output << "[Active]\nSource=" << state.source << '\n' << SerializeEditorTheme(state.active) << '\n';
        for (const auto &theme : state.custom)
            output << "[Custom]\n" << SerializeEditorTheme(theme) << '\n';
    }
}
