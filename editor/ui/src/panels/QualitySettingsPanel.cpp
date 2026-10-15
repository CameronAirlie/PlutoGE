#include "PlutoGE/ui/panels/QualitySettingsPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RhiRenderService.h"

namespace PlutoGE::ui
{
    namespace
    {
        void Help(const char *text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", text);
        }
        bool Effect(const char *label, bool &value, const char *description)
        {
            const bool changed = ImGui::Checkbox(label, &value);
            Help(description);
            return changed;
        }
    }

    void QualitySettingsPanel::Render()
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        if (!project) { ImGui::TextUnformatted("Open a project to edit quality tiers."); return; }
        auto &engine = core::Engine::GetInstance();
        ImGui::TextWrapped("Define what each player quality tier permits. Definitions are saved with the project and included in builds.");
        ImGui::TextDisabled("Save with File > Save Project (Ctrl+S).");
        ImGui::Combo("Quality tier", &m_tier, "Low\0Medium\0High\0Ultra\0");
        auto &presets = project->GetManifest().graphicsQualityPresets;
        auto quality = presets.tiers[static_cast<std::size_t>(m_tier)];
        const bool playing = engine.IsRuntimeRunning();
        if (playing) ImGui::TextWrapped("Stop Play to edit project quality definitions.");
        ImGui::BeginDisabled(playing);
        bool changed = ImGui::Checkbox("Apply tier overrides", &quality.enabled);
        Help("When off, this tier uses scene-authored settings without quality overrides.");
        ImGui::BeginDisabled(!quality.enabled);
        ImGui::SeparatorText("Shadow policy");
        ImGui::TextWrapped("Limits cap scene-authored settings. A tier never enables disabled scene shadows or effects.");
        int resolution = static_cast<int>(quality.shadowResolution);
        int cascades = static_cast<int>(quality.shadowCascades);
        changed |= ImGui::SliderInt("Resolution ceiling", &resolution, 256, 8192, "%d", ImGuiSliderFlags_AlwaysClamp);
        quality.shadowResolution = static_cast<std::uint32_t>(resolution);
        Help("Caps cascaded directional shadow-map resolution. Virtual-shadow budgets and local shadows are unchanged.");
        changed |= ImGui::SliderInt("Cascade ceiling", &cascades, 1, 4, "%d", ImGuiSliderFlags_AlwaysClamp);
        quality.shadowCascades = static_cast<std::uint32_t>(cascades);
        changed |= ImGui::SliderFloat("Distance ceiling", &quality.shadowDistance, 1.0f, 10000.0f, "%.1f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        Help("Caps directional shadow distance and any authored positive caster-distance limit, in world units.");
        changed |= ImGui::Checkbox("Use cascaded shadows", &quality.cascadedShadows);
        Help("On: select cascaded directional shadows. Off: retain the scene's authored shadow method.");
        ImGui::SeparatorText("Allowed scene effects");
        ImGui::TextWrapped("Checked keeps an effect if the scene enables it. Unchecked skips the listed effect group. Effect strengths and sample counts remain scene-authored.");
        changed |= Effect("Ambient occlusion", quality.ambientOcclusion, "Allows SSAO.");
        changed |= Effect("Global illumination", quality.globalIllumination, "Allows SSGI and voxel cone tracing GI.");
        changed |= Effect("Screen-space reflections", quality.reflections, "Allows SSR; material environment reflections remain available.");
        changed |= Effect("Volumetrics", quality.volumetrics, "Allows volumetric fog and volumetric clouds.");
        changed |= Effect("Bloom and lens flare", quality.bloom, "Allows bloom and lens flare.");
        changed |= Effect("Depth of field", quality.depthOfField, "Allows the depth-of-field post-process effect.");
        changed |= Effect("Motion blur", quality.motionBlur, "Allows the motion-blur post-process effect.");
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::Button("Reset this tier to engine defaults"))
        {
            quality = render::GraphicsQuality::FromPreset(static_cast<render::GraphicsPreset>(m_tier));
            changed = true;
        }
        if (changed && quality.IsValid())
        {
            presets.tiers[static_cast<std::size_t>(m_tier)] = quality;
            engine.GetRhiRenderService().SetGraphicsQualityPresets(presets);
            shell.MarkProjectDirty();
        }
        ImGui::EndDisabled();
        if (ImGui::Button("Preview tier in Scene viewport") && m_preview) m_preview(quality);
        Help("Requires RHI preview. Applies this tier to the Scene viewport only. Use the Game viewport Quality popup to preview there.");
        ImGui::TextWrapped("Edits update preset definitions. Apply a tier again to update an existing preview or runtime selection. Display resolution, VSync, upscaling, texture residency, particles and mesh LOD are independent.");
    }
}
