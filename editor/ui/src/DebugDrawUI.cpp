#include "PlutoGE/ui/DebugDrawUI.h"
#include "PlutoGE/render/DebugDraw.h"
#include <imgui.h>

namespace PlutoGE::ui
{
    namespace { bool show = false; AmbientProbeDebugSettings probes; }
    AmbientProbeDebugSettings &GetAmbientProbeDebugSettings() { return probes; }
    void OpenDebugDrawControls() { show = true; }
    void RenderDebugDrawControls()
    {
        if (!show) return;
        if (!ImGui::Begin("Viewport Debug Drawing", &show)) { ImGui::End(); return; }
        if (ImGui::CollapsingHeader("Ambient & Reflection Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Diffuse probe spheres", &probes.diffuse);
            ImGui::Checkbox("Reflection capture spheres", &probes.reflections);
            ImGui::Checkbox("Rejected probes (red)", &probes.rejected);
            ImGui::SliderFloat("Diffuse sphere radius", &probes.diffuseRadius,0.03f,1.0f);
            ImGui::SliderFloat("Reflection sphere radius", &probes.reflectionRadius,0.1f,3.0f);
            ImGui::SliderFloat("Reflection preview roughness", &probes.reflectionRoughness,0.0f,1.0f);
            ImGui::SliderInt("Diffuse display limit", &probes.maxDiffuseSpheres,16,2048);
            ImGui::TextWrapped("Scene viewport only (Vulkan/OpenGL RHI). Diffuse spheres show stored SH lighting; chrome spheres show each capture's HDR cubemap. Red marks rejected solid cells. Nearest probes are shown first. Spheres are depth tested and excluded from baking, captures and saved scenes.");
        }
        ImGui::Separator();
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
