#include "PlutoGE/ui/SourceTextEditor.h"
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

namespace PlutoGE::ui
{
    bool EditSourceText(const char *id, std::string &source, const ImVec2 &size)
    {
        if (auto *state = ImGui::GetInputTextState(ImGui::GetID(id)); state && source != state->GetText())
            state->ReloadUserBufAndKeepSelection();
        return ImGui::InputTextMultiline(id, &source, size,
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_NoUndoRedo);
    }
}
