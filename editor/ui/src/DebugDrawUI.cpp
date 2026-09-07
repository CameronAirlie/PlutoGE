#include "PlutoGE/ui/DebugDrawUI.h"
#include "PlutoGE/render/DebugDraw.h"
#include <imgui.h>

namespace PlutoGE::ui
{
    namespace { bool show = false; }
    void OpenDebugDrawControls() { show = true; }
    void RenderDebugDrawControls()
    {
        if (!show) return;
        if (!ImGui::Begin("Gameplay Debug Drawing", &show)) { ImGui::End(); return; }
        auto &draw = render::DebugDraw::Get();
        bool enabled = draw.IsEnabled();
        if (ImGui::Checkbox("Show gameplay overlays", &enabled)) draw.SetEnabled(enabled);
        ImGui::TextWrapped("Lines, wire spheres and labels in Scene and Game views. Draws through geometry. Lifetimes use simulation time and freeze at time scale zero.");
        ImGui::Text("Native rejected submissions: %zu", draw.Dropped());
        if (ImGui::Button("Clear drawings and categories")) draw.Clear();
        ImGui::Separator();
        for (auto [category, visible] : draw.Categories())
        {
            ImGui::PushID(category.c_str());
            if (ImGui::Checkbox("##visible", &visible)) draw.SetCategoryVisible(category, visible);
            ImGui::SameLine();
            ImGui::TextUnformatted(category.c_str());
            ImGui::PopID();
        }
        ImGui::End();
    }
}
