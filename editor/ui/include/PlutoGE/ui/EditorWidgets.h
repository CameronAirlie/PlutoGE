#pragma once

#include <imgui.h>

#include <span>

// Theme-aware controls for settings-style UI. They draw with the active
// EditorTheme colours, so they follow theme changes like built-in widgets.
namespace PlutoGE::ui::widgets
{
    bool ToggleSwitch(const char *id, bool *value);
    bool SegmentedControl(const char *id, int *current, std::span<const char *const> labels, float width = -1.0f);
    bool ColorSwatch(const char *id, const ImVec4 &color, bool selected, float diameter = 0.0f);
    bool NavItem(const char *icon, const char *label, bool selected);
    bool PrimaryButton(const char *label, const ImVec2 &size = ImVec2(0.0f, 0.0f));
    bool IconButton(const char *id, const char *icon, const char *tooltip = nullptr);
    void Pill(const char *text, const ImVec4 &color);

    void PageTitle(const char *title, const char *subtitle = nullptr);
    void SectionHeading(const char *title, const char *subtitle = nullptr);

    // A raised, rounded container that sizes to its content.
    void BeginCard(const char *id);
    void EndCard();

    // Two-column label/control layout. Call SettingsRow before each control.
    bool BeginSettingsTable(const char *id);
    void SettingsRow(const char *label, const char *description = nullptr);
    void EndSettingsTable();
}
