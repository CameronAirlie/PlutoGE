#include "PlutoGE/ui/panels/RmlDocumentEditorPanel.h"
#include "PlutoGE/ui/RmlSourceTools.h"
#include "PlutoGE/ui/SourceTextEditor.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace PlutoGE::ui
{
    void RmlDocumentEditorPanel::RenderVisualAuthoring()
    {
        if (!ImGui::CollapsingHeader("Visual UI builder", ImGuiTreeNodeFlags_DefaultOpen)) return;
        try
        {
            const auto source = m_session.GetBuffers().front().source;
            const auto elements = RmlSourceTools::Parse(source);
            if (elements.empty()) return;
            m_selectedElement = std::min(m_selectedElement, elements.size() - 1);
            if (ImGui::BeginChild("Element hierarchy", {0, 150}, ImGuiChildFlags_Borders))
            {
                for (std::size_t i = 0; i < elements.size(); ++i)
                {
                    const auto &element = elements[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Indent(static_cast<float>(element.depth) * 12);
                    const auto label = element.tag + (element.id.empty() ? "" : " #" + element.id);
                    if (ImGui::Selectable(label.c_str(), m_selectedElement == i)) m_selectedElement = i;
                    ImGui::Unindent(static_cast<float>(element.depth) * 12);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            const auto &element = elements[m_selectedElement];
            for (const auto *name : {"id", "class", "style", "value", "src"})
            {
                std::array<char, 4096> buffer{};
                const auto attribute = std::find_if(element.attributes.begin(), element.attributes.end(), [&](const auto &a) { return a.name == name; });
                if (attribute != element.attributes.end())
                {
                    if (attribute->value.size() >= buffer.size()) { ImGui::TextWrapped("%s is too long for this field; use source editing.", name); continue; }
                    std::strncpy(buffer.data(), attribute->value.c_str(), buffer.size() - 1);
                }
                if (ImGui::InputText(name, buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    m_session.SetSource(0, RmlSourceTools::SetAttribute(source, m_selectedElement, name, buffer.data()));
                    m_error.clear();
                    return;
                }
            }
            ImGui::TextDisabled("Enter commits an attribute edit. Use dp for screen sizes.");
            const char *properties[] = {"width", "height", "left", "right", "top", "bottom", "padding", "margin", "font-size", "color", "background-color", "display", "justify-content", "align-items"};
            ImGui::Combo("Style property", &m_styleProperty, properties, 14);
            ImGui::InputTextWithHint("Style value", "e.g. 240dp", m_styleValue.data(), m_styleValue.size());
            if (ImGui::Button("Apply style"))
            {
                m_session.SetSource(0, RmlSourceTools::SetStyle(source, m_selectedElement, properties[m_styleProperty], m_styleValue.data()));
                return;
            }
            ImGui::InputText("Plain text", m_elementText.data(), m_elementText.size());
            if (ImGui::Button("Set leaf text"))
            {
                m_session.SetSource(0, RmlSourceTools::SetText(source, m_selectedElement, m_elementText.data()));
                return;
            }
            for (const auto &line : m_preview.InspectElement(element.id)) ImGui::TextWrapped("%s", line.c_str());
            const char *types[] = {"div", "button", "label", "input", "img", "progress"};
            ImGui::Combo("Widget", &m_widgetType, types, 6);
            ImGui::InputTextWithHint("New ID", "unique-widget-id", m_widgetId.data(), m_widgetId.size());
            if (ImGui::Button("Insert child"))
            {
                m_session.SetSource(0, RmlSourceTools::Insert(source, m_selectedElement, types[m_widgetType], m_widgetId.data()));
                m_error.clear(); return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete element"))
            {
                m_session.SetSource(0, RmlSourceTools::Remove(source, m_selectedElement));
                m_selectedElement = 0; m_error.clear(); return;
            }
        }
        catch (const std::exception &error) { ImGui::TextWrapped("%s", error.what()); }
    }
}
