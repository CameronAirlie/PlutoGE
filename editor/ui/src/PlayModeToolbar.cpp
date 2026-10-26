#include "PlutoGE/ui/PlayModeToolbar.h"

#include <algorithm>
#include <array>

namespace PlutoGE::ui::PlayModeToolbar
{
    namespace
    {
        constexpr ImVec4 kPlayingAccent(0.30f, 0.78f, 0.42f, 1.0f);
        constexpr ImVec4 kPausedAccent(0.98f, 0.70f, 0.20f, 1.0f);

        enum class Glyph
        {
            Play,
            Pause,
            Step,
            Stop,
        };

        ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        void DrawGlyph(ImDrawList &draw, Glyph glyph, ImVec2 center, float size, ImU32 color)
        {
            const float h = size * 0.5f;
            switch (glyph)
            {
            case Glyph::Play:
                draw.AddTriangleFilled(ImVec2(center.x - h * 0.8f, center.y - h), ImVec2(center.x - h * 0.8f, center.y + h),
                                       ImVec2(center.x + h, center.y), color);
                break;
            case Glyph::Pause:
                draw.AddRectFilled(ImVec2(center.x - h * 0.85f, center.y - h), ImVec2(center.x - h * 0.25f, center.y + h), color, 1.0f);
                draw.AddRectFilled(ImVec2(center.x + h * 0.25f, center.y - h), ImVec2(center.x + h * 0.85f, center.y + h), color, 1.0f);
                break;
            case Glyph::Step:
                draw.AddTriangleFilled(ImVec2(center.x - h, center.y - h), ImVec2(center.x - h, center.y + h),
                                       ImVec2(center.x + h * 0.45f, center.y), color);
                draw.AddRectFilled(ImVec2(center.x + h * 0.5f, center.y - h), ImVec2(center.x + h, center.y + h), color, 1.0f);
                break;
            case Glyph::Stop:
                draw.AddRectFilled(ImVec2(center.x - h * 0.85f, center.y - h * 0.85f), ImVec2(center.x + h * 0.85f, center.y + h * 0.85f),
                                   color, 2.0f);
                break;
            }
        }

        // An icon button; `active` fills it with the accent to show the current state.
        bool GlyphButton(const char *id, Glyph glyph, ImVec2 size, bool enabled, std::optional<ImVec4> active,
                         const char *tooltip)
        {
            ImGui::BeginDisabled(!enabled);
            const bool pressed = ImGui::InvisibleButton(id, size);
            ImGui::EndDisabled();
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            auto &draw = *ImGui::GetWindowDrawList();
            const ImGuiStyle &style = ImGui::GetStyle();
            if (active)
                draw.AddRectFilled(min, max, ImGui::GetColorU32(*active), style.FrameRounding);
            else if (enabled && hovered)
                draw.AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_ButtonHovered), style.FrameRounding);
            ImVec4 color = active ? ImVec4(0.06f, 0.07f, 0.08f, 1.0f) : style.Colors[ImGuiCol_Text];
            if (!enabled)
                color.w *= style.DisabledAlpha;
            DrawGlyph(draw, glyph, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), size.y * 0.5f,
                      ImGui::GetColorU32(color));
            if (hovered)
                ImGui::SetTooltip("%s", tooltip);
            return pressed && enabled;
        }
    }

    std::optional<ImVec4> GetAccent(PlayState state)
    {
        switch (state)
        {
        case PlayState::Playing: return kPlayingAccent;
        case PlayState::Paused: return kPausedAccent;
        case PlayState::Editing: break;
        }
        return std::nullopt;
    }

    int PushMenuBarTint(PlayState state)
    {
        const auto accent = GetAccent(state);
        if (!accent)
            return 0;
        const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, Mix(base, ImVec4(accent->x, accent->y, accent->z, base.w), 0.28f));
        return 1;
    }

    PlayModeToolbarActions Draw(PlayState state, bool canPlay)
    {
        PlayModeToolbarActions actions;
        const bool running = state != PlayState::Editing;
        const float height = ImGui::GetFrameHeight();
        const ImVec2 buttonSize(height * 1.5f, height);
        const float spacing = 2.0f;
        const float groupWidth = buttonSize.x * 4.0f + spacing * 3.0f;

        // Centre on the window, but never over the menus already drawn.
        const float centered = (ImGui::GetWindowWidth() - groupWidth) * 0.5f;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), centered));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0.0f));

        if (GlyphButton("##PlayModePlay", Glyph::Play, buttonSize, canPlay || running,
                        state == PlayState::Playing ? GetAccent(state) : std::nullopt,
                        running ? "Stop (Shift+F5)" : "Play (F5)"))
            (running ? actions.stop : actions.play) = true;
        ImGui::SameLine();
        if (GlyphButton("##PlayModePause", Glyph::Pause, buttonSize, running,
                        state == PlayState::Paused ? GetAccent(state) : std::nullopt,
                        state == PlayState::Paused ? "Resume (Ctrl+Shift+P)" : "Pause (Ctrl+Shift+P)"))
            actions.togglePause = true;
        ImGui::SameLine();
        if (GlyphButton("##PlayModeStep", Glyph::Step, buttonSize, state == PlayState::Paused, std::nullopt,
                        "Step one frame (Ctrl+Alt+P)"))
            actions.step = true;
        ImGui::SameLine();
        if (GlyphButton("##PlayModeStop", Glyph::Stop, buttonSize, running, std::nullopt, "Stop (Shift+F5)"))
            actions.stop = true;

        ImGui::PopStyleVar();
        ImGui::SameLine();
        return actions;
    }

    void DrawViewportFrame(ImDrawList &drawList, ImVec2 min, ImVec2 max, PlayState state)
    {
        const auto accent = GetAccent(state);
        if (!accent)
            return;
        const ImU32 color = ImGui::GetColorU32(*accent);
        constexpr float thickness = 3.0f;
        drawList.AddRect(ImVec2(min.x + thickness * 0.5f, min.y + thickness * 0.5f),
                         ImVec2(max.x - thickness * 0.5f, max.y - thickness * 0.5f), color, 0.0f, 0, thickness);

        const char *label = state == PlayState::Paused ? "PAUSED" : "PLAYING";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 padding(8.0f, 3.0f);
        const ImVec2 badgeMin((min.x + max.x - textSize.x) * 0.5f - padding.x, min.y);
        const ImVec2 badgeMax(badgeMin.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f);
        drawList.AddRectFilled(badgeMin, badgeMax, color, 4.0f, ImDrawFlags_RoundCornersBottom);
        drawList.AddText(ImVec2(badgeMin.x + padding.x, badgeMin.y + padding.y), IM_COL32(16, 18, 20, 255), label);
    }
}
