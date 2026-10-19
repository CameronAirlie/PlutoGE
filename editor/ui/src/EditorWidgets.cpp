#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace PlutoGE::ui::widgets
{
    namespace
    {
        ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        ImU32 U32(const ImVec4 &color) { return ImGui::ColorConvertFloat4ToU32(color); }

        float BaseFontSize()
        {
            const float base = ImGui::GetStyle().FontSizeBase;
            return base > 0.0f ? base : ImGui::GetFontSize();
        }

        // Eases a per-widget value toward `target` for small state transitions.
        float Animate(ImGuiID id, float target)
        {
            ImGuiStorage *storage = ImGui::GetStateStorage();
            const ImGuiID key = ImHashStr("##anim", 0, id);
            float value = storage->GetFloat(key, target);
            value += (target - value) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
            if (std::abs(target - value) < 0.002f)
                value = target;
            storage->SetFloat(key, value);
            return value;
        }
    }

    bool ToggleSwitch(const char *id, bool *value)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const ImGuiID itemId = window->GetID(id);
        const float frameHeight = ImGui::GetFrameHeight();
        const float height = IM_ROUND(frameHeight * 0.72f);
        const float width = IM_ROUND(height * 1.8f);
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + width, pos.y + frameHeight));
        ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
        if (!ImGui::ItemAdd(bb, itemId))
            return false;

        bool hovered = false;
        bool held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);
        if (pressed)
        {
            *value = !*value;
            ImGui::MarkItemEdited(itemId);
        }

        const float t = Animate(itemId, *value ? 1.0f : 0.0f);
        const ImRect track(ImVec2(bb.Min.x, bb.GetCenter().y - height * 0.5f),
                           ImVec2(bb.Max.x, bb.GetCenter().y + height * 0.5f));
        const ImVec4 off = ImGui::GetStyleColorVec4(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        const ImVec4 on = GetEditorColor(hovered ? EditorColor::AccentHovered : EditorColor::Accent);
        ImDrawList *drawList = window->DrawList;
        drawList->AddRectFilled(track.Min, track.Max, U32(Mix(off, on, t)), height * 0.5f);
        if (t < 1.0f)
            drawList->AddRect(track.Min, track.Max, GetEditorColorU32(EditorColor::Border, 1.0f - t), height * 0.5f);

        const float radius = height * 0.5f - 3.0f;
        const float knobX = ImLerp(track.Min.x + height * 0.5f, track.Max.x - height * 0.5f, t);
        const ImVec4 knob = Mix(GetEditorColor(EditorColor::TextMuted), GetEditorColor(EditorColor::AccentText), t);
        drawList->AddCircleFilled(ImVec2(knobX, track.GetCenter().y), radius, U32(knob));
        ImGui::RenderNavCursor(bb, itemId);
        return pressed;
    }

    bool SegmentedControl(const char *id, int *current, std::span<const char *const> labels, float width)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems || labels.empty())
            return false;

        const ImGuiStyle &style = ImGui::GetStyle();
        ImGui::PushID(id);
        const float totalWidth = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + totalWidth, pos.y + height));
        ImGui::ItemSize(bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, window->GetID("##segments")))
        {
            ImGui::PopID();
            return false;
        }

        ImDrawList *drawList = window->DrawList;
        const float rounding = style.FrameRounding;
        drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
        if (style.FrameBorderSize > 0.0f)
            drawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Border), rounding);

        bool changed = false;
        const float segmentWidth = totalWidth / static_cast<float>(labels.size());
        const float inset = 2.0f;
        for (int i = 0; i < static_cast<int>(labels.size()); ++i)
        {
            const ImRect segment(ImVec2(bb.Min.x + segmentWidth * static_cast<float>(i), bb.Min.y),
                                 ImVec2(bb.Min.x + segmentWidth * static_cast<float>(i + 1), bb.Max.y));
            const ImGuiID segmentId = window->GetID(i);
            ImGui::KeepAliveID(segmentId);
            bool hovered = false;
            bool held = false;
            if (ImGui::ButtonBehavior(segment, segmentId, &hovered, &held) && *current != i)
            {
                *current = i;
                changed = true;
            }

            const bool selected = *current == i;
            const ImRect fill(ImVec2(segment.Min.x + inset, segment.Min.y + inset),
                              ImVec2(segment.Max.x - inset, segment.Max.y - inset));
            if (selected)
                drawList->AddRectFilled(fill.Min, fill.Max, GetEditorColorU32(EditorColor::Accent), std::max(0.0f, rounding - 1.0f));
            else if (hovered)
                drawList->AddRectFilled(fill.Min, fill.Max, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), std::max(0.0f, rounding - 1.0f));

            const char *labelEnd = ImGui::FindRenderedTextEnd(labels[i]);
            const ImVec2 textSize = ImGui::CalcTextSize(labels[i], labelEnd);
            const ImVec2 textPos(segment.GetCenter().x - textSize.x * 0.5f, segment.GetCenter().y - textSize.y * 0.5f);
            drawList->AddText(textPos, selected ? GetEditorColorU32(EditorColor::AccentText)
                                                : ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                              labels[i], labelEnd);
        }
        if (changed)
            ImGui::MarkItemEdited(window->GetID("##segments"));
        ImGui::PopID();
        return changed;
    }

    bool ColorSwatch(const char *id, const ImVec4 &color, bool selected, float diameter)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const float size = diameter > 0.0f ? diameter : ImGui::GetFrameHeight();
        const float ring = 3.0f;
        const ImGuiID itemId = window->GetID(id);
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + size + ring * 2.0f, pos.y + size + ring * 2.0f));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, itemId))
            return false;

        bool hovered = false;
        bool held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);
        const ImVec2 center = bb.GetCenter();
        ImDrawList *drawList = window->DrawList;
        if (selected || hovered)
            drawList->AddCircle(center, size * 0.5f + ring - 0.5f,
                                selected ? GetEditorColorU32(EditorColor::Text) : GetEditorColorU32(EditorColor::TextMuted, 0.6f),
                                0, 1.5f);
        drawList->AddCircleFilled(center, size * 0.5f - (selected ? 1.0f : 0.0f), U32(ImVec4(color.x, color.y, color.z, 1.0f)));
        if (selected)
        {
            const float luminance = 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
            const ImVec2 iconSize = ImGui::CalcTextSize(icons::Check);
            drawList->AddText(ImVec2(center.x - iconSize.x * 0.5f, center.y - iconSize.y * 0.5f),
                              luminance > 0.6f ? IM_COL32(17, 18, 22, 255) : IM_COL32_WHITE, icons::Check);
        }
        ImGui::RenderNavCursor(bb, itemId);
        return pressed;
    }

    bool NavItem(const char *icon, const char *label, bool selected)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const ImGuiStyle &style = ImGui::GetStyle();
        const ImGuiID itemId = window->GetID(label);
        const float height = IM_ROUND(ImGui::GetFrameHeight() * 1.35f);
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y + height));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, itemId))
            return false;

        bool hovered = false;
        bool held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);
        ImDrawList *drawList = window->DrawList;
        const float rounding = std::max(style.FrameRounding, 4.0f);
        if (selected)
        {
            drawList->AddRectFilled(bb.Min, bb.Max, GetEditorColorU32(EditorColor::Selection), rounding);
            drawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + height * 0.25f), ImVec2(bb.Min.x + 3.0f, bb.Max.y - height * 0.25f),
                                    GetEditorColorU32(EditorColor::Accent), 2.0f);
        }
        else if (hovered)
        {
            drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_HeaderHovered), rounding);
        }

        const float iconColumn = ImGui::GetFontSize() * 2.2f;
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        drawList->AddText(ImVec2(bb.Min.x + 12.0f + (iconColumn - 12.0f - iconSize.x) * 0.5f, bb.GetCenter().y - iconSize.y * 0.5f),
                          selected ? GetEditorColorU32(EditorColor::Accent) : GetEditorColorU32(EditorColor::TextMuted), icon);
        const char *labelEnd = ImGui::FindRenderedTextEnd(label);
        const ImVec2 labelSize = ImGui::CalcTextSize(label, labelEnd);
        drawList->AddText(ImVec2(bb.Min.x + iconColumn + 4.0f, bb.GetCenter().y - labelSize.y * 0.5f),
                          GetEditorColorU32(selected || hovered ? EditorColor::Text : EditorColor::TextMuted), label, labelEnd);
        ImGui::RenderNavCursor(bb, itemId);
        return pressed;
    }

    bool PrimaryButton(const char *label, const ImVec2 &size)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, GetEditorColor(EditorColor::Accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetEditorColor(EditorColor::AccentHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetEditorColor(EditorColor::Accent));
        ImGui::PushStyleColor(ImGuiCol_Border, GetEditorColor(EditorColor::Accent));
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::AccentText));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(5);
        return pressed;
    }

    bool IconButton(const char *id, const char *icon, const char *tooltip)
    {
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const float size = ImGui::GetFrameHeight();
        const bool pressed = ImGui::Button(icon, ImVec2(size, size));
        ImGui::PopStyleColor(2);
        if (tooltip != nullptr && tooltip[0] != '\0')
            ImGui::SetItemTooltip("%s", tooltip);
        ImGui::PopID();
        return pressed;
    }

    void Pill(const char *text, const ImVec4 &color)
    {
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 padding(8.0f, 2.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float frameOffset = (ImGui::GetFrameHeight() - textSize.y - padding.y * 2.0f) * 0.5f;
        const ImVec2 min(pos.x, pos.y + std::max(0.0f, frameOffset));
        const ImVec2 max(min.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f);
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, U32(ImVec4(color.x, color.y, color.z, 0.18f)), (max.y - min.y) * 0.5f);
        drawList->AddText(ImVec2(min.x + padding.x, min.y + padding.y), U32(color), text);
        ImGui::Dummy(ImVec2(max.x - pos.x, std::max(ImGui::GetFrameHeight(), max.y - pos.y)));
    }

    void PageTitle(const char *title, const char *subtitle)
    {
        ImGui::PushFont(nullptr, BaseFontSize() * 1.5f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        if (subtitle != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextWrapped("%s", subtitle);
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 2.0f));
    }

    void SectionHeading(const char *title, const char *subtitle)
    {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushFont(nullptr, BaseFontSize() * 1.12f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        if (subtitle != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextWrapped("%s", subtitle);
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.5f));
    }

    void BeginCard(const char *id)
    {
        const ImGuiStyle &style = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Mix(GetEditorColor(EditorColor::Panel), GetEditorColor(EditorColor::Surface), 0.55f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, std::max(style.ChildRounding, 2.0f) + 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
        ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    }

    void EndCard()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    bool BeginSettingsTable(const char *id)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 8.0f));
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::PopStyleVar();
            return false;
        }
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        return true;
    }

    void SettingsRow(const char *label, const char *description)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (description != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 12.0f);
            ImGui::TextUnformatted(description);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void EndSettingsTable()
    {
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }
}
