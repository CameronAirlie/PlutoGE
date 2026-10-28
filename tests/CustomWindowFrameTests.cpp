#include "PlutoGE/platform/CustomWindowFrame.h"
#include "PlutoGE/platform/Window.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#endif

namespace
{
    using namespace PlutoGE::platform;

    int TestHitTesting()
    {
        CaptionLayout layout;
        layout.caption = {0.0f, 0.0f, 800.0f, 32.0f};
        layout.interactive.push_back({40.0f, 4.0f, 300.0f, 28.0f});
        layout.maximizeButton = {708.0f, 0.0f, 754.0f, 32.0f};

        if (HitTestCaption(layout, 400.0f, 16.0f, 800.0f, 0.0f) != CaptionHit::Caption) return 101;
        if (HitTestCaption(layout, 100.0f, 16.0f, 800.0f, 0.0f) != CaptionHit::Client) return 102;
        if (HitTestCaption(layout, 720.0f, 16.0f, 800.0f, 0.0f) != CaptionHit::MaximizeButton) return 103;
        if (HitTestCaption(layout, 400.0f, 100.0f, 800.0f, 0.0f) != CaptionHit::Client) return 104;
        // The top resize band wins over the caption, with wider corners.
        if (HitTestCaption(layout, 400.0f, 2.0f, 800.0f, 6.0f) != CaptionHit::ResizeTop) return 105;
        if (HitTestCaption(layout, 8.0f, 2.0f, 800.0f, 6.0f) != CaptionHit::ResizeTopLeft) return 106;
        if (HitTestCaption(layout, 795.0f, 2.0f, 800.0f, 6.0f) != CaptionHit::ResizeTopRight) return 107;
        if (HitTestCaption(layout, 720.0f, 2.0f, 800.0f, 6.0f) != CaptionHit::ResizeTop) return 108;
        // Without a resize band (maximised) the top row belongs to the caption.
        if (HitTestCaption(layout, 400.0f, 2.0f, 800.0f, 0.0f) != CaptionHit::Caption) return 109;
        if (HitTestCaption({}, 10.0f, 10.0f, 800.0f, 0.0f) != CaptionHit::Client) return 110;
        return 0;
    }

#ifdef _WIN32
    int TestNativeFrame()
    {
        Window window;
        if (!window.Create({.title = "PlutoGE custom frame test",
                            .width = 640,
                            .height = 480,
                            .visible = false,
                            .customTitleBar = true,
                            .clientApi = WindowClientApi::None}))
            return 201;
        auto *native = static_cast<GLFWwindow *>(window.GetWindow());
        CustomWindowFrame *frame = window.GetCustomFrame();
        if (!frame || !frame->IsActive() || frame->GetWindow() != native) return 202;

        // The system caption became client area: the client starts at the window's top edge.
        const HWND hwnd = glfwGetWin32Window(native);
        RECT outer{};
        GetWindowRect(hwnd, &outer);
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        if (origin.y != outer.top) return 203;

        frame->SetClientSize(500, 360);
        int width = 0, height = 0;
        glfwGetWindowSize(native, &width, &height);
        if (width != 500 || height != 360) return 204;
        frame->SetClientPosition(120, 140);
        int x = 0, y = 0;
        glfwGetWindowPos(native, &x, &y);
        if (x != 120 || y != 140) return 205;
        if (!window.SetWindowedSize(560, 400)) return 206;

        // Borderless fullscreen suspends the frame; leaving it restores the exact client rectangle.
        window.SetFullscreen(true);
        if (frame->IsActive()) return 207;
        window.SetFullscreen(false);
        if (!frame->IsActive()) return 208;
        glfwGetWindowSize(native, &width, &height);
        glfwGetWindowPos(native, &x, &y);
        if (width != 560 || height != 400 || x != 120 || y != 140) return 209;

        frame->Detach();
        if (frame->IsActive() || frame->GetWindow()) return 210;
        // With the system caption back, the client no longer starts at the top edge.
        GetWindowRect(hwnd, &outer);
        origin = {0, 0};
        ClientToScreen(hwnd, &origin);
        if (origin.y == outer.top) return 211;
        window.Close();
        return 0;
    }
#endif
}

int main()
{
    if (const int result = TestHitTesting()) return result;
#ifdef _WIN32
    if (!CustomWindowFrame::IsSupported()) return 1;
    if (const int result = TestNativeFrame()) return result;
#else
    if (CustomWindowFrame::IsSupported()) return 1;
#endif
    return 0;
}
