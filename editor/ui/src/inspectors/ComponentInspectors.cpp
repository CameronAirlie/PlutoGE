#include "PlutoGE/ui/ComponentInspectors.h"

#include <imgui.h>

#include <unordered_map>

namespace PlutoGE::ui
{
    namespace
    {
        std::unordered_map<scene::ComponentTypeID, ComponentInspector> &Storage()
        {
            static std::unordered_map<scene::ComponentTypeID, ComponentInspector> inspectors;
            return inspectors;
        }

        std::vector<PropertyAnnotator> &Annotators()
        {
            static std::vector<PropertyAnnotator> annotators;
            return annotators;
        }

        // Built-in inspectors register on first lookup. Kept apart from
        // Storage() because registration itself writes to the storage.
        void EnsureBuiltins()
        {
            static const bool registered = (RegisterUIComponentInspectors(), RegisterUIStyleInspectors(), true);
            (void)registered;
        }
    }

    void RegisterComponentInspector(scene::ComponentTypeID type, ComponentInspector inspector)
    {
        Storage()[type] = inspector;
    }

    ComponentInspector FindComponentInspector(scene::ComponentTypeID type)
    {
        EnsureBuiltins();
        const auto found = Storage().find(type);
        return found == Storage().end() ? nullptr : found->second;
    }

    void RegisterPropertyAnnotator(PropertyAnnotator annotator)
    {
        Annotators().push_back(annotator);
    }

    std::optional<PropertyAnnotation> AnnotateProperty(scene::Component &component, ComponentInspectorContext &context,
                                                       const scene::Property &property)
    {
        EnsureBuiltins();
        for (const auto annotator : Annotators())
            if (auto annotation = annotator(component, context, property))
                return annotation;
        return std::nullopt;
    }

    void DrawPropertyAnnotation(const PropertyAnnotation &annotation)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec4 color = annotation.kind == PropertyAnnotation::Kind::Driven ? ImVec4(0.42f, 0.62f, 1.0f, 1.0f)
                                                                                 : ImVec4(0.98f, 0.7f, 0.25f, 1.0f);
        // A thin bar just left of the control, in the indent, so layout does not move.
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x - 5.0f, min.y + 1.0f), ImVec2(min.x - 2.0f, max.y - 1.0f),
                                                  ImGui::GetColorU32(color), 1.0f);
        // Callers scope each property with its own ID, so one popup name serves all.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (!annotation.tooltip.empty())
                ImGui::SetTooltip("%s%s", annotation.tooltip.c_str(), annotation.actions.empty() ? "" : "\nRight-click for options.");
            if (!annotation.actions.empty() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##PropertyAnnotation");
        }
        if (ImGui::BeginPopup("##PropertyAnnotation"))
        {
            for (const auto &action : annotation.actions)
                if (ImGui::MenuItem(action.label.c_str()) && action.run)
                    action.run();
            ImGui::EndPopup();
        }
    }
}
