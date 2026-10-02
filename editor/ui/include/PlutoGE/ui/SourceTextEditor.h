#pragma once
#include <imgui.h>
#include <string>

namespace PlutoGE::ui
{
    // The caller owns source and undo history. External edits (undo, reload,
    // future property tools) are reflected even while this widget is active.
    bool EditSourceText(const char *id, std::string &source, const ImVec2 &size);
}
