#pragma once

#include "PlutoGE/platform/CustomWindowFrame.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace PlutoGE::ui::caption
{
    // Building blocks shared by every editor title bar: Windows 11 caption
    // proportions, theme-derived colours, the window-control buttons and the
    // hit-test layout reported to the native frame.

    struct Metrics
    {
        float height = 32.0f;
        float buttonWidth = 46.0f;
        float glyphSize = 10.0f;
        float stroke = 1.0f;
    };

    // Native proportions (a 46x32 button on a 32 px caption at 100% scale)
    // sized from the current font, so they follow DPI and the editor font size.
    // Compact captions suit detached tool windows.
    [[nodiscard]] Metrics ComputeMetrics(bool compact = false);

    struct Colors
    {
        ImU32 background = 0;
        ImU32 text = 0;
        ImU32 textMuted = 0;
        ImU32 border = 0;
        ImU32 accent = 0;
        ImU32 buttonHovered = 0;
        ImU32 buttonPressed = 0;
        ImU32 closeHovered = 0;
        ImU32 closePressed = 0;
        ImU32 closeGlyph = 0;
    };

    // Colours from the active editor theme. `tint` blends the background
    // toward an accent, e.g. while play mode is running. Text is dimmed for
    // unfocused windows, as the system caption does.
    [[nodiscard]] Colors GetColors(bool focused, std::optional<ImVec4> tint = std::nullopt);

    enum class Glyph
    {
        Minimize,
        Maximize,
        Restore,
        Close,
        Pin,
        Pinned,
        DockIntoMain,
    };

    void DrawGlyph(ImDrawList &drawList, Glyph glyph, ImVec2 center, const Metrics &metrics, ImU32 color);
    // The PlutoGE mark: a planet with a tilted orbit.
    void DrawAppMark(ImDrawList &drawList, ImVec2 center, float size, const Colors &colors);

    // Accumulates where a title bar drew itself this frame, in screen space,
    // and reports it to the viewport's native frame in client coordinates.
    class LayoutBuilder
    {
    public:
        explicit LayoutBuilder(const ImGuiViewport &viewport) : m_origin(viewport.Pos) {}

        void SetCaption(ImVec2 min, ImVec2 max) { m_layout.caption = ToClient(min, max); }
        void AddInteractive(ImVec2 min, ImVec2 max) { m_layout.interactive.push_back(ToClient(min, max)); }
        // Marks the last submitted ImGui item as interactive.
        void AddLastItem() { AddInteractive(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }
        void SetMaximizeButton(ImVec2 min, ImVec2 max) { m_layout.maximizeButton = ToClient(min, max); }
        void Commit(platform::CustomWindowFrame &frame) { frame.SetCaptionLayout(std::move(m_layout)); }

    private:
        [[nodiscard]] platform::FrameRect ToClient(ImVec2 min, ImVec2 max) const
        {
            return {min.x - m_origin.x, min.y - m_origin.y, max.x - m_origin.x, max.y - m_origin.y};
        }

        ImVec2 m_origin;
        platform::CaptionLayout m_layout;
    };

    struct ButtonStyle
    {
        bool destructive = false; // Close: red on hover, as on Windows.
        bool checked = false;     // Toggles such as Always on Top.
    };

    // A flat caption button covering `min`..`max`, drawn in the current window.
    // `forceHovered`/`forcePressed` carry state the OS observed for buttons
    // that receive non-client input (the maximize button).
    bool Button(const char *id, Glyph glyph, ImVec2 min, ImVec2 max, const Metrics &metrics, const Colors &colors,
                const ButtonStyle &style = {}, const char *tooltip = nullptr, bool forceHovered = false,
                bool forcePressed = false);

    // Minimize, Maximize/Restore and Close, right-aligned to `topRight`.
    // Commands go through the native frame, exactly like the system caption.
    // Returns the left edge of the group.
    float WindowControls(platform::CustomWindowFrame &frame, ImVec2 topRight, const Metrics &metrics,
                         const Colors &colors, LayoutBuilder &layout, bool showMinimize = true);

    // Whether the platform window of `viewport` has keyboard focus.
    [[nodiscard]] bool IsViewportFocused(ImGuiViewport *viewport);

    // Shortens `text` with an ellipsis to fit `maxWidth`; empty when not even that fits.
    [[nodiscard]] std::string Elide(std::string_view text, float maxWidth);
}
