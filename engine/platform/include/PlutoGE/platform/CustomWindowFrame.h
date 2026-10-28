#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct GLFWwindow;

namespace PlutoGE::platform
{
    // A rectangle in window client coordinates: pixels from the top-left of
    // the client area, the space GLFW reports cursor positions in.
    struct FrameRect
    {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;

        [[nodiscard]] bool IsEmpty() const { return maxX <= minX || maxY <= minY; }
        [[nodiscard]] bool Contains(float x, float y) const { return x >= minX && x < maxX && y >= minY && y < maxY; }
    };

    // Where the application drew its title bar in the most recent frame.
    struct CaptionLayout
    {
        // The whole title bar. Points inside it drag the window unless they
        // fall in `interactive` or on the maximize button.
        FrameRect caption;
        // Menus, buttons and other controls that take ordinary client input.
        std::vector<FrameRect> interactive;
        // Reported to the OS as its own maximize button, so Windows 11 offers
        // snap layouts on hover. Empty when the caption has no such button.
        FrameRect maximizeButton;
    };

    enum class CaptionHit
    {
        Client,
        Caption,
        MaximizeButton,
        ResizeTop,
        ResizeTopLeft,
        ResizeTopRight,
    };

    // Classifies a client point against a caption layout. `resizeBorder` is the
    // depth of the top resize band, or 0 when the window cannot be resized
    // from its top edge (maximised or fixed size).
    [[nodiscard]] CaptionHit HitTestCaption(const CaptionLayout &layout, float x, float y,
                                            float clientWidth, float resizeBorder);

    enum class WindowCommand
    {
        Minimize,
        Maximize,
        Restore,
        Close,
    };

    // Lets the parts the OS still draws match the application theme.
    struct FrameAppearance
    {
        bool dark = true;
        // 0xRRGGBB for the Windows 11 window border; empty keeps the system colour.
        std::optional<std::uint32_t> borderColor;
    };

    // Replaces a native window's system caption with one the application
    // draws, while keeping the behaviour of the OS frame: resize borders, drop
    // shadow, rounded corners, drag-to-snap, double-click to maximise, the
    // system menu, snap layouts and the minimise/maximise animations.
    //
    // Each frame the application reports where its caption sits through
    // SetCaptionLayout; the OS hit tests are answered from that description.
    // Commands are posted to the window, so they take effect in the next
    // event poll exactly as if the user had used the system caption.
    //
    // Only Windows is supported. Elsewhere Attach fails and callers keep the
    // system title bar.
    class CustomWindowFrame
    {
    public:
        CustomWindowFrame();
        ~CustomWindowFrame();
        CustomWindowFrame(const CustomWindowFrame &) = delete;
        CustomWindowFrame &operator=(const CustomWindowFrame &) = delete;

        [[nodiscard]] static bool IsSupported();

        bool Attach(GLFWwindow *window);
        void Detach();
        [[nodiscard]] GLFWwindow *GetWindow() const;
        // Attached to a window that currently has a system caption to replace.
        // False while the window is borderless, for example in fullscreen.
        [[nodiscard]] bool IsActive() const;

        void SetCaptionLayout(CaptionLayout layout);
        void SetAppearance(const FrameAppearance &appearance);

        // The maximize button receives non-client input, so the application
        // cannot observe hover or press on it directly.
        [[nodiscard]] bool IsMaximizeHovered() const;
        [[nodiscard]] bool IsMaximizePressed() const;

        [[nodiscard]] bool IsWindowMaximized() const;
        void Execute(WindowCommand command);
        // Opens the system menu (Restore, Move, Size, ... Close) at a client point.
        void ShowSystemMenu(float clientX, float clientY);

        // GLFW's position and size setters assume a system caption and would
        // leave the client area too tall; these place the client area exactly.
        void SetClientPosition(int x, int y);
        void SetClientSize(int width, int height);

        struct State;

    private:
        std::unique_ptr<State> m_state;
    };
}
