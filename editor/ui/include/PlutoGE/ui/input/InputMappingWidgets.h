#pragma once

#include "PlutoGE/ui/InputMappingDocument.h"

#include <imgui.h>

#include <span>
#include <string>
#include <string_view>

// Controls shared by the input mapping views.
namespace PlutoGE::ui::input
{
    [[nodiscard]] const char *BindingKindIcon(InputBindingKind kind);
    // "Gamepad Button" - the kind as people read it, not as the JSON spells it.
    [[nodiscard]] const char *BindingKindLabel(InputBindingKind kind);
    // "LeftShift" -> "Left Shift", "D7" -> "7": a JSON input name as people read it.
    [[nodiscard]] std::string ReadableInputName(std::string_view name);
    // The input alone, e.g. "Space", "Left Shift (-)" for a negative scale, "A  ·  Pad 1"; triggers are shown separately.
    [[nodiscard]] std::string BindingInputLabel(const InputMappingBinding &binding);
    // One line per trigger kind explaining when it fires.
    [[nodiscard]] const char *TriggerKindHint(InputTriggerKind kind);

    // A rounded key-cap showing a binding's device icon and input. Fixed
    // height of one frame; returns true when clicked. `highlighted` tints it
    // with the accent (a selected binding, a button held down).
    bool BindingChip(const char *id, const InputMappingBinding &binding, bool highlighted = false);

    // Combo over a name table with a search box for long lists (keys). A value
    // this editor does not list shows as "(unlisted)" and is kept unchanged.
    bool InputNameCombo(const char *id, std::string &value, std::span<const InputNamedCode> names);

    // A label/control table for binding settings: fixed label column, the
    // control filling the rest. Call PropertyRow before each control.
    bool BeginPropertyTable(const char *id);
    void PropertyRow(const char *label, const char *tooltip = nullptr);
    void EndPropertyTable();

    // Trigger kind as a segmented control plus that kind's settings, laid out
    // as rows of the current property table. Returns true when it changed.
    bool TriggerRows(InputMappingTrigger &trigger);
}
