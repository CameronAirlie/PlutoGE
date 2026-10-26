#include "PlutoGE/ui/ComponentInspectors.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UIDropdownComponent.h"
#include "PlutoGE/scene/components/UIInputFieldComponent.h"
#include "PlutoGE/scene/components/UILayoutComponents.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

// Custom inspectors for the native UI components: friendlier controls for a
// few properties and context-sensitive filtering of the rest, which the
// generic property editor still draws.
namespace PlutoGE::ui
{
    namespace
    {
        void Remove(std::vector<scene::Property> &properties, std::initializer_list<std::string_view> names)
        {
            std::erase_if(properties, [&](const scene::Property &property)
                          { return std::find(names.begin(), names.end(), property.name) != names.end(); });
        }

        void Changed(ComponentInspectorContext &context, const char *type, std::initializer_list<const char *> properties)
        {
            for (const char *property : properties)
                context.entity.AddPrefabOverride(std::string("Component:") + type + ":" + property);
            context.shell.MarkSceneDirty();
        }

        int ResizeCallback(ImGuiInputTextCallbackData *data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto *text = static_cast<std::string *>(data->UserData);
                text->resize(static_cast<std::size_t>(data->BufTextLen));
                data->Buf = text->data();
            }
            return 0;
        }

        // An unbounded text box (the generic string editor stops at 256 bytes).
        bool EditText(const char *label, std::string &value, bool multiline, float lines = 4.0f)
        {
            const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
            if (multiline)
                return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1,
                                                 ImVec2(-1.0f, ImGui::GetTextLineHeight() * lines + 8.0f), flags,
                                                 ResizeCallback, &value);
            return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ResizeCallback, &value);
        }

        // A 3x3 grid of small toggle buttons; returns the clicked cell or -1.
        int CellGrid(const char *id, int selected, const char *const (&labels)[9])
        {
            int clicked = -1;
            ImGui::PushID(id);
            for (int index = 0; index < 9; ++index)
            {
                if (index % 3) ImGui::SameLine(0.0f, 2.0f);
                const bool active = index == selected;
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(labels[index], ImVec2(30.0f, 22.0f)))
                    clicked = index;
                if (active) ImGui::PopStyleColor();
            }
            ImGui::PopID();
            return clicked;
        }

        void InspectRectTransform(scene::Component &component, ComponentInspectorContext &context,
                                  std::vector<scene::Property> &properties)
        {
            auto &rect = static_cast<scene::RectTransformComponent &>(component);
            ImGui::TextDisabled("Anchor preset");
            constexpr const char *kLabels[9] = {"TL", "T", "TR", "L", "C", "R", "BL", "B", "BR"};
            const int current = static_cast<int>(rect.GetAnchorPreset());
            int chosen = CellGrid("Anchors", current, kLabels);
            ImGui::SameLine();
            const bool stretch = current == static_cast<int>(scene::UIAnchorPreset::Stretch);
            if (stretch) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button("Stretch", ImVec2(64.0f, 22.0f)))
                chosen = static_cast<int>(scene::UIAnchorPreset::Stretch);
            if (stretch) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fill the parent; Size Delta becomes the inset");
            if (chosen >= 0)
            {
                rect.SetAnchorPreset(static_cast<scene::UIAnchorPreset>(chosen));
                rect.SetAnchoredPosition(glm::vec2(0.0f));
                if (chosen == static_cast<int>(scene::UIAnchorPreset::Stretch))
                    rect.SetSizeDelta(glm::vec2(0.0f));
                Changed(context, "RectTransformComponent", {"AnchorPreset", "AnchorMin", "AnchorMax", "Pivot", "AnchoredPosition", "SizeDelta"});
            }
            Remove(properties, {"AnchorPreset"});
            // The RectTransform's own layout group predates UILayoutComponent.
            if (rect.GetLayoutMode() == scene::UILayoutMode::None)
                Remove(properties, {"LayoutMode", "LayoutPadding", "LayoutSpacing", "GridColumns", "ControlChildWidth",
                                    "ControlChildHeight", "ExpandChildWidth", "ExpandChildHeight"});
            else
                ImGui::TextDisabled("Legacy layout group: a UI Layout component offers flex and grid layout.");
        }

        void InspectLayout(scene::Component &component, ComponentInspectorContext &, std::vector<scene::Property> &properties)
        {
            const auto mode = static_cast<scene::UILayoutComponent &>(component).GetMode();
            if (mode != LayoutMode::Flex)
                Remove(properties, {"Direction", "Wrap", "Justify", "AlignContent"});
            if (mode != LayoutMode::Grid)
                Remove(properties, {"Columns", "Rows", "AutoRow", "JustifyItems"});
            if (mode == LayoutMode::None)
                Remove(properties, {"AlignItems", "Gap", "Padding"});
            if (mode == LayoutMode::Grid)
                ImGui::TextDisabled("Tracks: pixels, Nfr (share of the rest) or auto, e.g. \"200 1fr auto\".");
        }

        void InspectShape(scene::Component &component, ComponentInspectorContext &, std::vector<scene::Property> &properties)
        {
            const auto &shape = static_cast<scene::UIShapeComponent &>(component);
            if (shape.GetFill().type == Paint::Type::Solid)
                Remove(properties, {"EndColor", "GradientAngle"});
            if (!shape.HasShadow())
                Remove(properties, {"ShadowColor", "ShadowOffset", "ShadowBlur", "ShadowSpread", "ShadowInset"});
            if (shape.GetBorderWidth() <= 0.0f)
                Remove(properties, {"BorderColor"});
        }

        void InspectText(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            auto &text = static_cast<scene::UITextComponent &>(component);
            std::string value = text.GetText();
            if (EditText("##Text", value, true))
            {
                text.SetText(value);
                Changed(context, "UITextComponent", {"Text"});
            }
            ImGui::TextDisabled("Alignment");
            constexpr const char *kLabels[9] = {"TL", "T", "TR", "L", "C", "R", "BL", "B", "BR"};
            if (const int chosen = CellGrid("TextAlignment", static_cast<int>(text.GetAlignment()), kLabels); chosen >= 0)
            {
                text.SetAlignment(static_cast<scene::UITextAlignment>(chosen));
                Changed(context, "UITextComponent", {"Alignment"});
            }
            if (text.IsRichText())
                ImGui::TextDisabled("Markup: <b> <i> <u> <s> <color=#RRGGBB> <size=N> <br>");
            Remove(properties, {"Text", "Alignment"});
            if (text.GetOutlineWidth() <= 0.0f)
                Remove(properties, {"OutlineColor"});
        }

        void InspectInputField(scene::Component &component, ComponentInspectorContext &context,
                               std::vector<scene::Property> &properties)
        {
            auto &field = static_cast<scene::UIInputFieldComponent &>(component);
            std::string value = field.GetText();
            if (EditText("Text", value, field.IsMultiline(), 3.0f))
            {
                field.SetText(value);
                Changed(context, "UIInputFieldComponent", {"Text"});
            }
            Remove(properties, {"Text"});
        }

        void InspectDropdown(scene::Component &component, ComponentInspectorContext &context,
                             std::vector<scene::Property> &properties)
        {
            auto &dropdown = static_cast<scene::UIDropdownComponent &>(component);
            ImGui::TextDisabled("Options (one per line)");
            std::string options;
            for (std::size_t index = 0; index < dropdown.GetOptions().size(); ++index)
                options += (index ? "\n" : "") + dropdown.GetOptions()[index];
            if (EditText("##Options", options, true, 5.0f))
            {
                std::vector<std::string> lines;
                std::string_view rest = options;
                while (!rest.empty())
                {
                    const auto end = rest.find('\n');
                    lines.emplace_back(rest.substr(0, end));
                    if (end == std::string_view::npos) break;
                    rest.remove_prefix(end + 1);
                }
                dropdown.SetOptions(std::move(lines));
                Changed(context, "UIDropdownComponent", {"Options", "Value"});
            }
            Remove(properties, {"Options"});
        }
    }

    void RegisterUIComponentInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::RectTransformComponent>(), &InspectRectTransform);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::UILayoutComponent>(), &InspectLayout);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::UIShapeComponent>(), &InspectShape);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::UITextComponent>(), &InspectText);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::UIInputFieldComponent>(), &InspectInputField);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::UIDropdownComponent>(), &InspectDropdown);
    }
}
