#include "PlutoGE/ui/WindowCaption.h"

#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace PlutoGE::ui::caption
{
    namespace
    {
        // Windows 11 caption at 100% scale.
        constexpr float kCaptionHeight = 32.0f;
        constexpr float kCompactCaptionHeight = 28.0f;
        constexpr float kButtonAspect = 46.0f / 32.0f;
        constexpr float kGlyphSize = 10.0f;
        constexpr float kPlayTintAmount = 0.28f;

        ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        ImU32 WithAlpha(ImVec4 color, float alpha)
        {
            color.w = alpha;
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        float Luminance(const ImVec4 &color)
        {
            return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
        }
    }

    Metrics ComputeMetrics(bool compact)
    {
        const float scale = ImGui::GetFontSize() / fonts::kBaseSize;
        Metrics metrics;
        metrics.height = std::max(std::round((compact ? kCompactCaptionHeight : kCaptionHeight) * scale),
                                  std::round(ImGui::GetFrameHeight() + 4.0f));
        metrics.buttonWidth = std::round(metrics.height * kButtonAspect);
        metrics.glyphSize = std::max(8.0f, std::round(kGlyphSize * scale));
        metrics.stroke = std::max(1.0f, std::round(scale));
        return metrics;
    }

    Colors GetColors(bool focused, std::optional<ImVec4> tint)
    {
        const ImGuiStyle &style = ImGui::GetStyle();
        ImVec4 background = style.Colors[ImGuiCol_MenuBarBg];
        background.w = 1.0f;
        if (tint)
            background = Mix(background, ImVec4(tint->x, tint->y, tint->z, 1.0f), kPlayTintAmount);
        const ImVec4 text = style.Colors[ImGuiCol_Text];
        const bool light = Luminance(background) > 0.5f;

        Colors colors;
        colors.background = ImGui::ColorConvertFloat4ToU32(background);
        // Unfocused windows grey out their caption text, as the system caption does.
        colors.text = ImGui::ColorConvertFloat4ToU32(focused ? text : Mix(text, background, 0.5f));
        colors.textMuted = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_TextDisabled]);
        colors.border = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Border]);
        colors.accent = GetEditorColorU32(EditorColor::Accent);
        colors.buttonHovered = WithAlpha(text, light ? 0.06f : 0.08f);
        colors.buttonPressed = WithAlpha(text, light ? 0.04f : 0.05f);
        colors.closeHovered = IM_COL32(196, 43, 28, 255);
        colors.closePressed = IM_COL32(196, 43, 28, 230);
        colors.closeGlyph = IM_COL32_WHITE;
        return colors;
    }

    void DrawGlyph(ImDrawList &drawList, Glyph glyph, ImVec2 center, const Metrics &metrics, ImU32 color)
    {
        // Snap to whole pixels so one-pixel strokes stay crisp.
        const float size = metrics.glyphSize;
        const float stroke = metrics.stroke;
        const float half = stroke * 0.5f;
        const float x0 = std::floor(center.x - size * 0.5f);
        const float y0 = std::floor(center.y - size * 0.5f);
        const float x1 = x0 + size;
        const float y1 = y0 + size;
        const float inset = std::round(size * 0.2f);
        const float rounding = std::max(1.0f, std::round(size * 0.12f));

        switch (glyph)
        {
        case Glyph::Minimize:
        {
            const float y = std::floor(center.y) + half;
            drawList.AddLine(ImVec2(x0, y), ImVec2(x1, y), color, stroke);
            break;
        }
        case Glyph::Maximize:
            drawList.AddRect(ImVec2(x0, y0), ImVec2(x1, y1), color, rounding, 0, stroke);
            break;
        case Glyph::Restore:
        {
            drawList.AddRect(ImVec2(x0, y0 + inset), ImVec2(x1 - inset, y1), color, rounding, 0, stroke);
            const ImVec2 back[] = {
                ImVec2(x0 + inset + half, y0 + inset),
                ImVec2(x0 + inset + half, y0 + half),
                ImVec2(x1 - half, y0 + half),
                ImVec2(x1 - half, y1 - inset - half),
                ImVec2(x1 - inset, y1 - inset - half),
            };
            drawList.AddPolyline(back, IM_ARRAYSIZE(back), color, ImDrawFlags_None, stroke);
            break;
        }
        case Glyph::Close:
            drawList.AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, stroke);
            drawList.AddLine(ImVec2(x1, y0), ImVec2(x0, y1), color, stroke);
            break;
        case Glyph::Pin:
        case Glyph::Pinned:
        {
            const float midX = std::floor(center.x) + half;
            const float headHalf = std::round(size * 0.28f);
            const float headBottom = y0 + std::round(size * 0.5f);
            const ImVec2 headMin(midX - headHalf, y0);
            const ImVec2 headMax(midX + headHalf, headBottom);
            if (glyph == Glyph::Pinned)
                drawList.AddRectFilled(headMin, headMax, color, rounding);
            else
                drawList.AddRect(headMin, headMax, color, rounding, 0, stroke);
            drawList.AddLine(ImVec2(x0 + inset * 0.5f, headBottom + half), ImVec2(x1 - inset * 0.5f, headBottom + half),
                             color, stroke);
            drawList.AddLine(ImVec2(midX, headBottom), ImVec2(midX, y1), color, stroke);
            break;
        }
        case Glyph::DockIntoMain:
        {
            drawList.AddRect(ImVec2(x0, y0 + inset), ImVec2(x1 - inset, y1), color, rounding, 0, stroke);
            const ImVec2 tip(x0 + std::round(size * 0.35f), y0 + std::round(size * 0.65f));
            const float head = std::round(size * 0.35f);
            drawList.AddLine(ImVec2(x1, y0), tip, color, stroke);
            drawList.AddLine(tip, ImVec2(tip.x + head, tip.y), color, stroke);
            drawList.AddLine(tip, ImVec2(tip.x, tip.y - head), color, stroke);
            break;
        }
        }
    }

    void DrawAppMark(ImDrawList &drawList, ImVec2 center, float size, const Colors &colors)
    {
        drawList.AddCircleFilled(center, size * 0.3f, colors.accent);
        drawList.AddEllipse(center, ImVec2(size * 0.52f, size * 0.17f), colors.text, -0.45f, 0,
                            std::max(1.0f, size * 0.08f));
    }

    bool Button(const char *id, Glyph glyph, ImVec2 min, ImVec2 max, const Metrics &metrics, const Colors &colors,
                const ButtonStyle &style, const char *tooltip, bool forceHovered, bool forcePressed)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;
        // Caption buttons are placed absolutely and never take keyboard navigation.
        const ImRect rect(min, max);
        const ImGuiID itemId = window->GetID(id);
        if (!ImGui::ItemAdd(rect, itemId, nullptr, ImGuiItemFlags_NoNav))
            return false;

        bool hovered = false;
        bool held = false;
        const bool pressed = ImGui::ButtonBehavior(rect, itemId, &hovered, &held);
        hovered = hovered || forceHovered;
        held = (held && hovered) || forcePressed;

        ImDrawList &drawList = *window->DrawList;
        ImU32 glyphColor = style.checked ? colors.accent : colors.text;
        if (style.destructive && (hovered || held))
        {
            drawList.AddRectFilled(min, max, held ? colors.closePressed : colors.closeHovered);
            glyphColor = colors.closeGlyph;
        }
        else if (held)
            drawList.AddRectFilled(min, max, colors.buttonPressed);
        else if (hovered)
            drawList.AddRectFilled(min, max, colors.buttonHovered);
        DrawGlyph(drawList, glyph, rect.GetCenter(), metrics, glyphColor);

        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tooltip);
        return pressed;
    }

    float WindowControls(platform::CustomWindowFrame &frame, ImVec2 topRight, const Metrics &metrics,
                         const Colors &colors, LayoutBuilder &layout, bool showMinimize)
    {
        using platform::WindowCommand;
        const ImVec2 size(metrics.buttonWidth, metrics.height);
        ImVec2 max(topRight.x, topRight.y + size.y);
        ImVec2 min(max.x - size.x, topRight.y);

        ImGui::PushID("##WindowControls");
        if (Button("Close", Glyph::Close, min, max, metrics, colors, {.destructive = true}, "Close"))
            frame.Execute(WindowCommand::Close);
        layout.AddInteractive(min, max);

        max.x = min.x;
        min.x -= size.x;
        // The OS owns input over this button; it toggles the window itself and
        // shows snap layouts on hover. Clicks only arrive here while the frame
        // has not received this layout yet.
        const bool maximized = frame.IsWindowMaximized();
        if (Button("Maximize", maximized ? Glyph::Restore : Glyph::Maximize, min, max, metrics, colors, {}, nullptr,
                   frame.IsMaximizeHovered(), frame.IsMaximizePressed()))
            frame.Execute(maximized ? WindowCommand::Restore : WindowCommand::Maximize);
        layout.SetMaximizeButton(min, max);

        if (showMinimize)
        {
            max.x = min.x;
            min.x -= size.x;
            if (Button("Minimize", Glyph::Minimize, min, max, metrics, colors, {}, "Minimize"))
                frame.Execute(WindowCommand::Minimize);
            layout.AddInteractive(min, max);
        }
        ImGui::PopID();
        return min.x;
    }

    bool IsViewportFocused(ImGuiViewport *viewport)
    {
        // A new viewport's OS window is created at the end of its first frame;
        // the backend cannot be queried before then. New windows take focus.
        const auto &platformIO = ImGui::GetPlatformIO();
        if (!viewport || !viewport->PlatformHandle || !platformIO.Platform_GetWindowFocus)
            return true;
        return platformIO.Platform_GetWindowFocus(viewport);
    }

    std::string Elide(std::string_view text, float maxWidth)
    {
        std::string shown(text);
        if (ImGui::CalcTextSize(shown.c_str()).x <= maxWidth)
            return shown;
        const float ellipsisWidth = ImGui::CalcTextSize("...").x;
        if (ellipsisWidth > maxWidth)
            return {};
        while (!shown.empty() && ImGui::CalcTextSize(shown.c_str()).x + ellipsisWidth > maxWidth)
        {
            // Drop whole UTF-8 sequences, never a lone continuation byte.
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80)
                shown.pop_back();
            if (!shown.empty())
                shown.pop_back();
        }
        return shown + "...";
    }
}
