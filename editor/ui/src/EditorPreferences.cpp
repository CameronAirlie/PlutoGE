#include "PlutoGE/ui/EditorPreferences.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        enum class PreferencesPage
        {
            Appearance,
            Colours,
            Interface,
            Advanced,
        };

        struct PreferencesState
        {
            PreferencesPage page = PreferencesPage::Appearance;
            std::array<char, 64> themeName{};
            std::array<char, 64> colorFilter{};
            std::string message;
            bool messageIsError = false;
            bool dirty = false;
            bool openSaveAs = false;
        };

        PreferencesState &Prefs()
        {
            static PreferencesState state;
            return state;
        }

        constexpr const char *kClipboardHeader = "[PlutoGE Theme]\n";

        struct AccentPreset
        {
            const char *name;
            ImVec4 color;
        };

        ImVec4 Rgb(unsigned int rgb)
        {
            return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(rgb & 0xFF) / 255.0f, 1.0f);
        }

        const std::array<AccentPreset, 9> &AccentPresets()
        {
            static const std::array<AccentPreset, 9> presets = {{
                {"Blue", Rgb(0x5B8CFF)},
                {"Indigo", Rgb(0x8B7BFF)},
                {"Violet", Rgb(0xB36BFF)},
                {"Pink", Rgb(0xEC5FA8)},
                {"Red", Rgb(0xF0605A)},
                {"Orange", Rgb(0xF29D38)},
                {"Amber", Rgb(0xF5C542)},
                {"Green", Rgb(0x4CC38A)},
                {"Teal", Rgb(0x3FB8C2)},
            }};
            return presets;
        }

        bool SameColor(const ImVec4 &a, const ImVec4 &b)
        {
            const auto byte = [](float value) { return static_cast<int>(value * 255.0f + 0.5f); };
            return byte(a.x) == byte(b.x) && byte(a.y) == byte(b.y) && byte(a.z) == byte(b.z) && byte(a.w) == byte(b.w);
        }

        void MarkEdited()
        {
            ApplyActiveEditorTheme();
            Prefs().dirty = true;
        }

        void SetMessage(std::string text, bool error = false)
        {
            Prefs().message = std::move(text);
            Prefs().messageIsError = error;
        }

        // Draws a miniature editor in `theme`'s colours so themes can be compared at a glance.
        bool ThemeCard(const EditorTheme &theme, bool selected, const ImVec2 &size)
        {
            ImGui::PushID(theme.name.c_str());
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton("##card", size);
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            const auto &p = theme.palette;
            const auto col = [](const ImVec4 &c, float alpha = 1.0f)
            { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); };
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float rounding = 8.0f;
            const ImVec2 max(pos.x + size.x, pos.y + size.y);
            const float labelHeight = ImGui::GetFrameHeight() + 4.0f;
            const ImVec2 previewMax(max.x, max.y - labelHeight);

            // Preview: chrome bar, sidebar, panel with a selected tab and content lines.
            drawList->AddRectFilled(pos, max, GetEditorColorU32(EditorColor::Surface), rounding);
            drawList->AddRectFilled(pos, previewMax, col(p.background), rounding, ImDrawFlags_RoundCornersTop);
            const float pad = 8.0f;
            const float barHeight = 10.0f;
            const ImVec2 bodyMin(pos.x + pad, pos.y + pad + barHeight);
            const ImVec2 bodyMax(previewMax.x - pad, previewMax.y - pad);
            for (int i = 0; i < 3; ++i)
                drawList->AddCircleFilled(ImVec2(pos.x + pad + 3.0f + i * 9.0f, pos.y + pad + 2.0f), 2.5f, col(p.textMuted, 0.6f));
            const float sidebarWidth = (bodyMax.x - bodyMin.x) * 0.32f;
            const float gap = 3.0f;
            drawList->AddRectFilled(bodyMin, ImVec2(bodyMin.x + sidebarWidth, bodyMax.y), col(p.panel), 3.0f);
            const ImVec2 mainMin(bodyMin.x + sidebarWidth + gap, bodyMin.y);
            drawList->AddRectFilled(mainMin, bodyMax, col(p.panel), 3.0f);
            drawList->AddRectFilled(mainMin, ImVec2(mainMin.x + 26.0f, mainMin.y + 2.0f), col(p.accent));
            for (int i = 0; i < 4; ++i)
            {
                const float y = bodyMin.y + 8.0f + i * 8.0f;
                if (y + 3.0f > bodyMax.y - 4.0f)
                    break;
                const ImVec4 &lineColor = i == 1 ? p.accent : p.textMuted;
                drawList->AddRectFilled(ImVec2(bodyMin.x + 5.0f, y), ImVec2(bodyMin.x + sidebarWidth - 6.0f - (i % 2) * 8.0f, y + 3.0f),
                                        col(lineColor, i == 1 ? 0.9f : 0.55f), 1.5f);
            }
            const float contentY = mainMin.y + 10.0f;
            drawList->AddRectFilled(ImVec2(mainMin.x + 6.0f, contentY), ImVec2(bodyMax.x - 22.0f, contentY + 4.0f), col(p.text, 0.8f), 2.0f);
            drawList->AddRectFilled(ImVec2(mainMin.x + 6.0f, contentY + 9.0f), ImVec2(bodyMax.x - 34.0f, contentY + 12.0f), col(p.textMuted, 0.6f), 1.5f);
            const ImVec2 fieldMin(mainMin.x + 6.0f, contentY + 18.0f);
            if (fieldMin.y + 9.0f < bodyMax.y)
            {
                drawList->AddRectFilled(fieldMin, ImVec2(bodyMax.x - 30.0f, fieldMin.y + 9.0f), col(p.surface), 2.0f);
                drawList->AddRect(fieldMin, ImVec2(bodyMax.x - 30.0f, fieldMin.y + 9.0f), col(p.border), 2.0f);
                drawList->AddRectFilled(ImVec2(bodyMax.x - 24.0f, fieldMin.y), ImVec2(bodyMax.x - 6.0f, fieldMin.y + 9.0f), col(p.accent), 4.5f);
            }

            // Label strip.
            const ImVec2 labelPos(pos.x + 10.0f, previewMax.y + (labelHeight - ImGui::GetFontSize()) * 0.5f);
            drawList->AddText(labelPos, GetEditorColorU32(EditorColor::Text), theme.name.c_str());
            if (selected)
            {
                const ImVec2 iconSize = ImGui::CalcTextSize(icons::Check);
                drawList->AddText(ImVec2(max.x - iconSize.x - 10.0f, labelPos.y), GetEditorColorU32(EditorColor::Accent), icons::Check);
            }

            const ImU32 outline = selected ? GetEditorColorU32(EditorColor::Accent)
                                  : hovered ? GetEditorColorU32(EditorColor::TextMuted, 0.7f)
                                            : GetEditorColorU32(EditorColor::Border);
            drawList->AddRect(pos, max, outline, rounding, 0, selected ? 2.0f : 1.0f);
            return pressed;
        }

        void RenderThemeGrid()
        {
            const ImVec2 cardSize(178.0f, 132.0f);
            const float spacing = 12.0f;
            const float available = ImGui::GetContentRegionAvail().x;
            const int columns = std::max(1, static_cast<int>((available + spacing) / (cardSize.x + spacing)));
            int index = 0;
            const auto renderCard = [&](const EditorTheme &theme)
            {
                if (index % columns != 0)
                    ImGui::SameLine(0.0f, spacing);
                ++index;
                const bool selected = GetActiveEditorThemeSource() == theme.name;
                if (ThemeCard(theme, selected, cardSize) && !selected)
                {
                    SelectEditorTheme(theme);
                    SetMessage({});
                }
            };
            for (const auto &theme : GetBuiltinEditorThemes())
                renderCard(theme);
            // Iterate a copy: card actions may add or remove custom themes.
            const auto customThemes = GetCustomEditorThemes();
            for (const auto &theme : customThemes)
                renderCard(theme);
        }

        void RenderThemeActions()
        {
            auto &prefs = Prefs();
            const std::string &source = GetActiveEditorThemeSource();
            const bool modified = IsActiveEditorThemeModified();
            const bool custom = !IsBuiltinEditorTheme(source) && FindEditorTheme(source) != nullptr;

            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Active theme");
            ImGui::SameLine();
            ImGui::TextUnformatted(source.c_str());
            if (modified)
            {
                ImGui::SameLine();
                widgets::Pill("Modified", GetEditorColor(EditorColor::Warning));
            }

            if (modified && FindEditorTheme(source) != nullptr)
            {
                if (ImGui::Button((std::string(icons::Revert) + "  Revert").c_str()))
                {
                    SelectEditorTheme(*FindEditorTheme(source));
                    SetMessage("Reverted to " + source + ".");
                }
                ImGui::SameLine();
            }
            if (custom && modified)
            {
                if (widgets::PrimaryButton((std::string(icons::Save) + "  Save changes").c_str()))
                {
                    SaveActiveThemeAsCustom(source);
                    SetMessage("Saved " + source + ".");
                }
                ImGui::SameLine();
            }
            if (ImGui::Button((std::string(icons::Plus) + "  Save as new theme").c_str()))
            {
                const std::string suggestion = custom ? source : source + " Custom";
                prefs.themeName.fill('\0');
                std::copy_n(suggestion.begin(), std::min(suggestion.size(), prefs.themeName.size() - 1), prefs.themeName.begin());
                prefs.openSaveAs = true;
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(icons::Copy) + "  Copy").c_str()))
            {
                ImGui::SetClipboardText((kClipboardHeader + SerializeEditorTheme(GetActiveEditorTheme())).c_str());
                SetMessage("Theme copied to the clipboard.");
            }
            ImGui::SetItemTooltip("Copy the active theme as text to share it.");
            ImGui::SameLine();
            if (ImGui::Button((std::string(icons::Paste) + "  Paste").c_str()))
            {
                EditorTheme imported;
                const char *clipboard = ImGui::GetClipboardText();
                if (clipboard != nullptr && std::string_view(clipboard).find("Name=") != std::string_view::npos &&
                    DeserializeEditorTheme(clipboard, imported))
                {
                    if (imported.name.empty())
                        imported.name = "Imported Theme";
                    SelectEditorTheme(imported);
                    SetMessage("Imported " + imported.name + ". Use Save as new theme to keep it.");
                }
                else
                {
                    SetMessage("The clipboard does not contain a PlutoGE theme.", true);
                }
            }
            ImGui::SetItemTooltip("Apply a theme copied from another editor.");
            if (custom)
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Error));
                if (ImGui::Button((std::string(icons::Trash) + "  Delete").c_str()))
                {
                    const std::string deleted = source;
                    DeleteCustomEditorTheme(deleted);
                    SelectEditorTheme(GetBuiltinEditorThemes().front());
                    SetMessage("Deleted " + deleted + ".");
                }
                ImGui::PopStyleColor();
            }

            if (!prefs.message.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(prefs.messageIsError ? EditorColor::Error : EditorColor::TextMuted));
                ImGui::TextWrapped("%s", prefs.message.c_str());
                ImGui::PopStyleColor();
            }

            if (prefs.openSaveAs)
            {
                ImGui::OpenPopup("Save Theme");
                prefs.openSaveAs = false;
            }
            ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f));
            if (ImGui::BeginPopupModal("Save Theme", nullptr, ImGuiWindowFlags_NoResize))
            {
                ImGui::TextUnformatted("Theme name");
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool submitted = ImGui::InputText("##ThemeName", prefs.themeName.data(), prefs.themeName.size(),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
                std::string name = prefs.themeName.data();
                while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                    name.pop_back();
                const bool reserved = IsBuiltinEditorTheme(name);
                const bool overwrites = !reserved && FindEditorTheme(name) != nullptr;
                if (reserved)
                    ImGui::TextColored(GetEditorColor(EditorColor::Error), "Built-in themes cannot be overwritten.");
                else if (overwrites)
                    ImGui::TextColored(GetEditorColor(EditorColor::Warning), "This replaces your existing theme.");
                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                const float buttonWidth = 96.0f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x);
                if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                    ImGui::CloseCurrentPopup();
                ImGui::SameLine();
                ImGui::BeginDisabled(name.empty() || reserved);
                if (widgets::PrimaryButton("Save", ImVec2(buttonWidth, 0.0f)) || (submitted && !name.empty() && !reserved))
                {
                    SaveActiveThemeAsCustom(name);
                    SetMessage("Saved " + name + ".");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
        }

        void RenderAccentPicker()
        {
            auto &accent = GetActiveEditorTheme().palette.accent;
            bool matchedPreset = false;
            for (std::size_t i = 0; i < AccentPresets().size(); ++i)
            {
                const auto &preset = AccentPresets()[i];
                const bool selected = SameColor(accent, preset.color);
                matchedPreset = matchedPreset || selected;
                if (i > 0)
                    ImGui::SameLine(0.0f, 6.0f);
                ImGui::PushID(static_cast<int>(i));
                if (widgets::ColorSwatch("##accent", preset.color, selected, 24.0f))
                {
                    accent = preset.color;
                    MarkEdited();
                }
                ImGui::SetItemTooltip("%s", preset.name);
                ImGui::PopID();
            }
            ImGui::SameLine(0.0f, 14.0f);
            const float offset = (24.0f + 6.0f - ImGui::GetFrameHeight()) * 0.5f;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, offset));
            if (ImGui::ColorEdit3("##CustomAccent", &accent.x,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoLabel))
                MarkEdited();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (matchedPreset)
                ImGui::TextDisabled("Custom");
            else
                ImGui::TextUnformatted("Custom");
        }

        void RenderPreviewWidgets()
        {
            static bool checkbox = true;
            static bool toggle = true;
            static int radio = 0;
            static float slider = 0.62f;
            static int combo = 0;
            static std::array<char, 32> text = {"Player_01"};

            if (widgets::BeginSettingsTable("##Preview"))
            {
                widgets::SettingsRow("Buttons");
                ImGui::Button("Secondary");
                ImGui::SameLine();
                widgets::PrimaryButton("Primary");
                widgets::SettingsRow("Selection");
                ImGui::Checkbox("Checkbox", &checkbox);
                ImGui::SameLine();
                ImGui::RadioButton("A", &radio, 0);
                ImGui::SameLine();
                ImGui::RadioButton("B", &radio, 1);
                ImGui::SameLine();
                widgets::ToggleSwitch("##PreviewToggle", &toggle);
                widgets::SettingsRow("Inputs");
                ImGui::InputText("##PreviewText", text.data(), text.size());
                widgets::SettingsRow("Values");
                ImGui::SliderFloat("##PreviewSlider", &slider, 0.0f, 1.0f, "%.2f");
                widgets::SettingsRow("Choice");
                constexpr std::array<const char *, 3> items = {"Perspective", "Orthographic", "Top"};
                ImGui::Combo("##PreviewCombo", &combo, items.data(), static_cast<int>(items.size()));
                widgets::SettingsRow("Progress");
                ImGui::ProgressBar(slider, ImVec2(-FLT_MIN, 0.0f));
                widgets::SettingsRow("Status");
                ImGui::TextColored(GetEditorColor(EditorColor::Success), "Build succeeded");
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "3 warnings");
                ImGui::TextColored(GetEditorColor(EditorColor::Error), "Missing material");
                widgets::EndSettingsTable();
            }
        }

        void RenderAppearancePage()
        {
            widgets::PageTitle("Appearance", "Pick a theme and accent colour. Changes apply immediately and are saved to your user profile.");
            RenderThemeActions();
            widgets::SectionHeading("Themes");
            RenderThemeGrid();
            widgets::SectionHeading("Accent colour", "Used for selection, focus, sliders and highlighted tabs.");
            RenderAccentPicker();
            widgets::SectionHeading("Preview");
            widgets::BeginCard("##PreviewCard");
            RenderPreviewWidgets();
            widgets::EndCard();
        }

        bool ColorRow(const char *label, const char *description, ImVec4 EditorThemePalette::*member)
        {
            auto &theme = GetActiveEditorTheme();
            ImVec4 &value = theme.palette.*member;
            widgets::SettingsRow(label, description);
            ImGui::PushID(label);
            const EditorTheme *source = FindEditorTheme(GetActiveEditorThemeSource());
            const bool canReset = source != nullptr && !SameColor(source->palette.*member, value);
            ImGui::SetNextItemWidth(canReset ? -ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x : -FLT_MIN);
            bool changed = ImGui::ColorEdit3("##color", &value.x, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueWheel);
            if (canReset)
            {
                ImGui::SameLine();
                if (widgets::IconButton("##reset", icons::Revert, "Reset to theme value"))
                {
                    value = source->palette.*member;
                    changed = true;
                }
            }
            ImGui::PopID();
            if (changed)
                MarkEdited();
            return changed;
        }

        void RenderColoursPage()
        {
            widgets::PageTitle("Colours", "Every editor colour is derived from this palette. Edit a value to restyle the whole interface.");
            auto &theme = GetActiveEditorTheme();

            widgets::SectionHeading("Surfaces");
            widgets::BeginCard("##Surfaces");
            if (widgets::BeginSettingsTable("##SurfaceTable"))
            {
                widgets::SettingsRow("Light theme", "Shade hover and pressed states darker instead of lighter.");
                if (widgets::ToggleSwitch("##light", &theme.light))
                    MarkEdited();
                ColorRow("Chrome", "Menu bar, tab strips and empty dock space.", &EditorThemePalette::background);
                ColorRow("Panel", "Panel and window bodies.", &EditorThemePalette::panel);
                ColorRow("Field", "Text fields, sliders and other inputs.", &EditorThemePalette::surface);
                ColorRow("Popup", "Menus, popups and tooltips.", &EditorThemePalette::elevated);
                ColorRow("Border", "Outlines, separators and table lines.", &EditorThemePalette::border);
                widgets::EndSettingsTable();
            }
            widgets::EndCard();

            widgets::SectionHeading("Text");
            widgets::BeginCard("##Text");
            if (widgets::BeginSettingsTable("##TextTable"))
            {
                ColorRow("Primary text", nullptr, &EditorThemePalette::text);
                ColorRow("Secondary text", "Hints, descriptions and disabled items.", &EditorThemePalette::textMuted);
                widgets::EndSettingsTable();
            }
            widgets::EndCard();

            widgets::SectionHeading("Accent and status");
            widgets::BeginCard("##Status");
            if (widgets::BeginSettingsTable("##StatusTable"))
            {
                ColorRow("Accent", "Selection, focus and active controls.", &EditorThemePalette::accent);
                ColorRow("Success", nullptr, &EditorThemePalette::success);
                ColorRow("Warning", "Also marks unsaved documents.", &EditorThemePalette::warning);
                ColorRow("Error", nullptr, &EditorThemePalette::error);
                widgets::EndSettingsTable();
            }
            widgets::EndCard();
        }

        void RenderInterfacePage()
        {
            widgets::PageTitle("Interface", "Spacing, shape and window chrome. These are saved with the active theme.");
            auto &metrics = GetActiveEditorTheme().metrics;

            widgets::SectionHeading("Layout");
            widgets::BeginCard("##Layout");
            if (widgets::BeginSettingsTable("##LayoutTable"))
            {
                widgets::SettingsRow("Density", "How much space surrounds controls and rows.");
                constexpr std::array<const char *, 3> densities = {"Compact", "Default", "Comfortable"};
                int density = static_cast<int>(metrics.density);
                if (widgets::SegmentedControl("##density", &density, densities))
                {
                    metrics.density = static_cast<EditorDensity>(density);
                    MarkEdited();
                }

                widgets::SettingsRow("Corners", "Rounding applied to panels, fields, buttons and tabs.");
                constexpr std::array<const char *, 4> cornerNames = {"Sharp", "Subtle", "Rounded", "Soft"};
                constexpr std::array<float, 4> cornerValues = {0.0f, 3.0f, 6.0f, 10.0f};
                int corner = -1;
                for (int i = 0; i < static_cast<int>(cornerValues.size()); ++i)
                    if (std::abs(metrics.cornerRadius - cornerValues[i]) < 0.05f)
                        corner = i;
                if (widgets::SegmentedControl("##corners", &corner, cornerNames))
                {
                    metrics.cornerRadius = cornerValues[static_cast<std::size_t>(corner)];
                    MarkEdited();
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##radius", &metrics.cornerRadius, 0.0f, 12.0f, "%.0f px"))
                    MarkEdited();

                widgets::SettingsRow("Scrollbar width");
                if (ImGui::SliderFloat("##scrollbar", &metrics.scrollbarSize, 6.0f, 18.0f, "%.0f px"))
                    MarkEdited();
                widgets::EndSettingsTable();
            }
            widgets::EndCard();

            widgets::SectionHeading("Details");
            widgets::BeginCard("##Details");
            if (widgets::BeginSettingsTable("##DetailsTable"))
            {
                widgets::SettingsRow("Outline fields", "Draw a border around inputs and buttons.");
                if (widgets::ToggleSwitch("##frameBorders", &metrics.frameBorders))
                    MarkEdited();
                widgets::SettingsRow("Tree guide lines", "Connect nested items in the hierarchy and other trees.");
                if (widgets::ToggleSwitch("##treeLines", &metrics.treeLines))
                    MarkEdited();
                widgets::SettingsRow("Highlight focused tab", "Draw an accent line above the selected tab of the focused panel.");
                if (widgets::ToggleSwitch("##tabOverline", &metrics.tabOverline))
                    MarkEdited();
                widgets::SettingsRow("Panel menu button", "Show the dock and collapse menu button in panel tab bars.");
                if (widgets::ToggleSwitch("##windowMenu", &metrics.windowMenuButton))
                    MarkEdited();
                widgets::EndSettingsTable();
            }
            widgets::EndCard();

            widgets::SectionHeading("Text");
            widgets::BeginCard("##Fonts");
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextWrapped("%s  The editor font and size are stored per project. Change them in File > Project Settings.", icons::Font);
            ImGui::PopStyleColor();
            widgets::EndCard();
        }

        void RenderAdvancedPage()
        {
            widgets::PageTitle("Advanced", "Pin individual ImGui style colours. Overrides take priority over the palette and are saved with the theme.");
            auto &prefs = Prefs();
            auto &theme = GetActiveEditorTheme();

            const float clearWidth = 160.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearWidth - ImGui::GetStyle().ItemSpacing.x);
            ImGui::InputTextWithHint("##ColorFilter", (std::string(icons::Search) + "  Filter colours").c_str(),
                                     prefs.colorFilter.data(), prefs.colorFilter.size());
            ImGui::SameLine();
            ImGui::BeginDisabled(theme.overrides.empty());
            const std::string clearLabel = "Clear overrides (" + std::to_string(theme.overrides.size()) + ")";
            if (ImGui::Button(clearLabel.c_str(), ImVec2(clearWidth, 0.0f)))
            {
                theme.overrides.clear();
                MarkEdited();
            }
            ImGui::EndDisabled();
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            std::string filter = prefs.colorFilter.data();
            std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            widgets::BeginCard("##Overrides");
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
            if (ImGui::BeginTable("##OverrideTable", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Colour", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                ImGui::TableHeadersRow();
                for (int i = 0; i < ImGuiCol_COUNT; ++i)
                {
                    const char *name = ImGui::GetStyleColorName(i);
                    if (!filter.empty())
                    {
                        std::string lower = name;
                        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (lower.find(filter) == std::string::npos)
                            continue;
                    }
                    ImGui::PushID(i);
                    const auto existing = theme.overrides.find(i);
                    const bool overridden = existing != theme.overrides.end();
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    if (overridden)
                        ImGui::TextColored(GetEditorColor(EditorColor::Accent), "%s", name);
                    else
                        ImGui::TextUnformatted(name);
                    ImGui::TableSetColumnIndex(1);
                    ImVec4 value = overridden ? existing->second : DeriveEditorThemeColor(theme, i);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::ColorEdit4("##value", &value.x,
                                          ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
                    {
                        theme.overrides[i] = value;
                        MarkEdited();
                    }
                    ImGui::TableSetColumnIndex(2);
                    if (overridden && widgets::IconButton("##reset", icons::Revert, "Remove override"))
                    {
                        theme.overrides.erase(i);
                        MarkEdited();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            widgets::EndCard();
        }
    }

    void RenderEditorPreferences(bool *open)
    {
        auto &prefs = Prefs();
        if (open == nullptr || !*open)
        {
            if (prefs.dirty)
            {
                SaveEditorThemeSettings();
                prefs.dirty = false;
            }
            return;
        }

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(960.0f, 680.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 420.0f), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool visible = ImGui::Begin((std::string(icons::Settings) + "  Preferences###EditorPreferences").c_str(), open,
                                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
        ImGui::PopStyleVar();
        if (visible)
        {
            // Sidebar.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, GetEditorColor(EditorColor::Background));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 14.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
            ImGui::BeginChild("##PreferencesNav", ImVec2(210.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
            ImGui::TextUnformatted("EDITOR");
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            const auto nav = [&](const char *icon, const char *label, PreferencesPage page)
            {
                if (widgets::NavItem(icon, label, prefs.page == page))
                    prefs.page = page;
            };
            nav(icons::Palette, "Appearance", PreferencesPage::Appearance);
            nav(icons::Swatches, "Colours", PreferencesPage::Colours);
            nav(icons::Quality, "Interface", PreferencesPage::Interface);
            nav(icons::Settings, "Advanced", PreferencesPage::Advanced);
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();

            ImGui::SameLine(0.0f, 0.0f);
            const ImVec2 dividerTop = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(dividerTop, ImVec2(dividerTop.x, dividerTop.y + ImGui::GetContentRegionAvail().y),
                                                GetEditorColorU32(EditorColor::Border));

            // Page content.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 22.0f));
            ImGui::BeginChild("##PreferencesPage", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            switch (prefs.page)
            {
            case PreferencesPage::Appearance: RenderAppearancePage(); break;
            case PreferencesPage::Colours: RenderColoursPage(); break;
            case PreferencesPage::Interface: RenderInterfacePage(); break;
            case PreferencesPage::Advanced: RenderAdvancedPage(); break;
            }
            ImGui::EndChild();
        }
        ImGui::End();

        // Write once an edit is released rather than every frame of a drag.
        if (prefs.dirty && !ImGui::IsAnyItemActive())
        {
            SaveEditorThemeSettings();
            prefs.dirty = false;
        }
    }

    void RenderEditorThemeMenu()
    {
        const auto item = [](const EditorTheme &theme)
        {
            const bool selected = GetActiveEditorThemeSource() == theme.name;
            if (ImGui::MenuItem(theme.name.c_str(), nullptr, selected) && !selected)
                SelectEditorTheme(theme);
        };
        for (const auto &theme : GetBuiltinEditorThemes())
            item(theme);
        if (!GetCustomEditorThemes().empty())
        {
            ImGui::Separator();
            const auto customThemes = GetCustomEditorThemes();
            for (const auto &theme : customThemes)
                item(theme);
        }
    }
}
