#include "PlutoGE/ui/WindInspectors.h"

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/WindComponent.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    namespace
    {
        // A plain-language name for a wind speed (Beaufort scale bands, m/s).
        const char *WindDescription(float speed)
        {
            constexpr std::array<std::pair<float, const char *>, 8> bands{{{0.3f, "Calm"},
                                                                          {1.6f, "Light air"},
                                                                          {3.4f, "Light breeze"},
                                                                          {5.5f, "Gentle breeze"},
                                                                          {8.0f, "Moderate breeze"},
                                                                          {10.8f, "Fresh breeze"},
                                                                          {13.9f, "Strong breeze"},
                                                                          {17.2f, "Near gale"}}};
            for (const auto &[limit, name] : bands)
                if (speed < limit)
                    return name;
            return "Gale";
        }

        // A small compass showing where the wind blows toward, matching the scene's X (right) and Z (down) axes as
        // seen from above.
        void DrawWindArrow(float degrees)
        {
            constexpr float radius = 14.0f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 center(origin.x + radius, origin.y + radius);
            auto *drawList = ImGui::GetWindowDrawList();
            const ImU32 ring = ImGui::GetColorU32(ImGuiCol_Border);
            const ImU32 arrow = ImGui::GetColorU32(ImGuiCol_PlotLines);
            drawList->AddCircle(center, radius, ring, 24, 1.0f);
            const float radians = degrees * 3.14159265f / 180.0f;
            const ImVec2 tip(center.x + std::cos(radians) * (radius - 2.0f), center.y + std::sin(radians) * (radius - 2.0f));
            const ImVec2 tail(center.x - std::cos(radians) * (radius - 4.0f), center.y - std::sin(radians) * (radius - 4.0f));
            drawList->AddLine(tail, tip, arrow, 2.0f);
            drawList->AddCircleFilled(tip, 3.0f, arrow);
            ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
        }

        void Changed(ComponentInspectorContext &context, std::string_view property)
        {
            context.entity.AddPrefabOverride("Component:WindComponent:" + std::string(property));
            context.shell.MarkSceneDirty();
        }

        void InspectWind(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            auto &wind = static_cast<scene::WindComponent &>(component);

            DrawWindArrow(wind.GetDirectionDegrees());
            ImGui::SameLine();
            ImGui::BeginGroup();
            float direction = wind.GetDirectionDegrees();
            if (ImGui::SliderFloat("Direction", &direction, 0.0f, 360.0f, "%.0f deg"))
            {
                wind.SetDirectionDegrees(direction);
                Changed(context, "Direction");
            }
            ImGui::SetItemTooltip("Where the wind blows toward: 0 is +X, 90 is +Z.");
            float speed = wind.GetSpeed();
            if (ImGui::SliderFloat("Speed", &speed, 0.0f, scene::WindComponent::MaxSpeed, "%.1f m/s"))
            {
                wind.SetSpeed(speed);
                Changed(context, "Speed");
            }
            ImGui::SetItemTooltip("%s. Foliage bends furthest from 20 m/s.", WindDescription(wind.GetSpeed()));
            ImGui::EndGroup();

            float gustiness = wind.GetGustiness();
            if (ImGui::SliderFloat("Gustiness", &gustiness, 0.0f, 1.0f, "%.2f"))
            {
                wind.SetGustiness(gustiness);
                Changed(context, "Gustiness");
            }
            ImGui::SetItemTooltip("0 is a steady wind; 1 swings between calm and twice the mean strength.");
            float gustSize = wind.GetGustSize();
            if (ImGui::DragFloat("Gust Size", &gustSize, 0.5f, scene::WindComponent::MinGustSize,
                                 scene::WindComponent::MaxGustSize, "%.1f m"))
            {
                wind.SetGustSize(gustSize);
                Changed(context, "Gust Size");
            }
            ImGui::SetItemTooltip("Distance between gust fronts, which roll downwind across fields at the wind speed.");

            std::erase_if(properties, [](const scene::Property &property) {
                for (const std::string_view name : {"Direction", "Speed", "Gustiness", "Gust Size"})
                    if (property.name == name)
                        return true;
                return false;
            });
        }
    }

    float FoliageWindMeshHeight(const render::Mesh *mesh)
    {
        if (!mesh)
            return 0.0f;
        float top = 0.0f;
        for (std::size_t index = 0; index < mesh->GetSubmeshCount(); ++index)
        {
            const auto &submesh = mesh->GetSubmesh(index);
            top = std::max(top, submesh.hasBoundsExtents ? submesh.boundsMax.y
                                                         : submesh.bounds.center.y + submesh.bounds.radius);
        }
        return top;
    }

    bool DrawFoliageWindSettings(render::FoliageWindResponse &wind, const render::Mesh *mesh, bool sceneHasWind)
    {
        bool changed = ImGui::Checkbox("Sway In Wind", &wind.enabled);
        ImGui::SetItemTooltip("Bend with the scene's Wind component in every pass, shadows included.");
        if (wind.enabled && !sceneHasWind)
            ImGui::TextDisabled("The scene is calm: add Environment > Wind to an entity.");

        ImGui::BeginDisabled(!wind.enabled);
        const float meshHeight = FoliageWindMeshHeight(mesh);
        struct Preset
        {
            const char *label;
            render::FoliageWindPreset preset;
        };
        static constexpr std::array presets{Preset{"Grass", render::FoliageWindPreset::Grass},
                                            Preset{"Flowers", render::FoliageWindPreset::Flowers},
                                            Preset{"Shrub", render::FoliageWindPreset::Shrub},
                                            Preset{"Tree", render::FoliageWindPreset::Tree}};
        ImGui::TextUnformatted("Preset");
        for (const auto &preset : presets)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(preset.label))
            {
                const float height = meshHeight > 0.0f ? meshHeight : wind.bendHeight;
                wind = render::FoliageWindPresetResponse(preset.preset, height);
                changed = true;
            }
        }

        changed |= ImGui::SliderFloat("Stiffness", &wind.stiffness, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("0 bends fully (grass); 1 does not sway (trunks). Stiffer plants also sway faster.");
        changed |= ImGui::SliderFloat("Flutter", &wind.flutter, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("Fast, small motion of leaves and blade tips.");
        changed |= ImGui::DragFloat("Bend Height", &wind.bendHeight, 0.01f, 0.01f, 200.0f, "%.2f");
        ImGui::SetItemTooltip("Mesh units above the origin where the sway reaches its full lean; about the plant's height.");
        if (meshHeight > 0.0f)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit"))
            {
                wind.bendHeight = meshHeight;
                changed = true;
            }
            ImGui::SetItemTooltip("Set the bend height to the mesh's height (%.2f).", meshHeight);
        }
        changed |= ImGui::DragFloat("Pivot Height", &wind.pivotHeight, 0.01f, -50.0f, 200.0f, "%.2f");
        ImGui::SetItemTooltip("Geometry at or below this height (mesh units) stays put, such as a trunk's base.");
        ImGui::EndDisabled();

        if (changed)
            wind = wind.Sanitized();
        return changed;
    }

    void RegisterWindInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::WindComponent>(), &InspectWind);
    }
}
