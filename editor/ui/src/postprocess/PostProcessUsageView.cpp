#include "PlutoGE/ui/postprocess/PostProcessViews.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>

namespace PlutoGE::ui::postprocess
{
    std::optional<int> DrawUsageView(const std::vector<PostProcessPresetUser> &users, bool editorCameraUsesPreset, bool &assignToEditorCamera)
    {
        std::optional<int> clicked;
        widgets::SectionHeading("Used By", "Cameras in the open scene, and the editor viewport. Saving updates all of them.");
        if (users.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextWrapped("Nothing in the open scene uses this preset. Select a camera and choose it under Post Processing, "
                               "or use it in the editor viewport.");
            ImGui::PopStyleColor();
        }
        for (int index = 0; index < static_cast<int>(users.size()); ++index)
        {
            const auto &user = users[index];
            ImGui::PushID(index);
            const char *icon = user.entity == 0 ? icons::View : icons::EntityCamera;
            const std::string label = std::string(icon) + "  " + user.label;
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowOverlap))
                clicked = index;
            ImGui::SetItemTooltip(user.entity == 0 ? "Select the editor camera" : "Select this camera in the scene");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", user.detail.c_str());
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(editorCameraUsesPreset);
        assignToEditorCamera = ImGui::Button((std::string(icons::View) + "  Use in Editor Viewport").c_str());
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(editorCameraUsesPreset ? "The editor viewport already uses this preset"
                                                     : "Preview the scene in the editor viewport with this preset");
        return clicked;
    }
}
