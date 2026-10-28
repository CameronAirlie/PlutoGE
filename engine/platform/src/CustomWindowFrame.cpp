#include "PlutoGE/platform/CustomWindowFrame.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif

#include <utility>

namespace PlutoGE::platform
{
    CaptionHit HitTestCaption(const CaptionLayout &layout, float x, float y, float clientWidth, float resizeBorder)
    {
        if (resizeBorder > 0.0f && y >= 0.0f && y < resizeBorder)
        {
            // Wider corner zones, as on the system frame.
            const float corner = resizeBorder * 2.0f;
            if (x < corner)
                return CaptionHit::ResizeTopLeft;
            if (x >= clientWidth - corner)
                return CaptionHit::ResizeTopRight;
            return CaptionHit::ResizeTop;
        }
        if (layout.maximizeButton.Contains(x, y))
            return CaptionHit::MaximizeButton;
        for (const auto &rect : layout.interactive)
            if (rect.Contains(x, y))
                return CaptionHit::Client;
        return layout.caption.Contains(x, y) ? CaptionHit::Caption : CaptionHit::Client;
    }

    struct CustomWindowFrame::State
    {
        GLFWwindow *window = nullptr;
        CaptionLayout layout;
        bool maximizeHovered = false;
        bool maximizePressed = false;
#ifdef _WIN32
        HWND hwnd = nullptr;
        bool trackingNonClientLeave = false;
#endif
    };

#ifdef _WIN32
    namespace
    {
        constexpr UINT_PTR kSubclassId = 0x504C4346; // "PLCF"

        // Not defined by older Windows SDKs; harmlessly rejected before Windows 11.
        constexpr DWORD kDwmUseImmersiveDarkMode = 20;
        constexpr DWORD kDwmBorderColor = 34;
        constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

        bool HasSystemCaption(HWND hwnd)
        {
            return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
        }

        bool CanResize(HWND hwnd)
        {
            return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0 && !IsZoomed(hwnd);
        }

        // Height of the sizing border the system frame would have, which a
        // maximised window pushes off-screen and we must keep clear of.
        int ResizeBorderHeight(HWND hwnd)
        {
            const UINT dpi = GetDpiForWindow(hwnd);
            return GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        }

        void ShowSystemMenuAt(HWND hwnd, POINT screen)
        {
            HMENU menu = GetSystemMenu(hwnd, FALSE);
            if (!menu)
                return;
            const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            const bool zoomed = IsZoomed(hwnd) != FALSE;
            const bool resizable = (style & WS_THICKFRAME) != 0;
            const auto enable = [menu](UINT command, bool enabled)
            {
                MENUITEMINFOW item{sizeof(item)};
                item.fMask = MIIM_STATE;
                item.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
                SetMenuItemInfoW(menu, command, FALSE, &item);
            };
            // The system keeps these in sync only for its own caption.
            enable(SC_RESTORE, zoomed);
            enable(SC_MOVE, !zoomed);
            enable(SC_SIZE, !zoomed && resizable);
            enable(SC_MINIMIZE, (style & WS_MINIMIZEBOX) != 0);
            enable(SC_MAXIMIZE, !zoomed && resizable && (style & WS_MAXIMIZEBOX) != 0);
            enable(SC_CLOSE, true);
            SetMenuDefaultItem(menu, SC_CLOSE, FALSE);

            const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
            const BOOL command = TrackPopupMenu(menu, TPM_RETURNCMD | alignment, screen.x, screen.y, 0, hwnd, nullptr);
            if (command != 0)
                PostMessageW(hwnd, WM_SYSCOMMAND, static_cast<WPARAM>(command), 0);
        }

        void ClearMaximizeState(CustomWindowFrame::State &state)
        {
            state.maximizeHovered = false;
            state.maximizePressed = false;
            state.trackingNonClientLeave = false;
        }

        LRESULT CALLBACK FrameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
        {
            auto &state = *reinterpret_cast<CustomWindowFrame::State *>(data);
            switch (message)
            {
            case WM_NCCALCSIZE:
            {
                if (wParam != TRUE || !HasSystemCaption(hwnd))
                    break;
                auto &client = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam)->rgrc[0];
                const LONG top = client.top;
                const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
                if (result != 0)
                    return result;
                // Keep the system's side and bottom borders, which carry the
                // resize handles and shadow, and give the caption to the client.
                client.top = top + (IsZoomed(hwnd) ? ResizeBorderHeight(hwnd) : 0);
                return 0;
            }
            case WM_NCHITTEST:
            {
                LRESULT hit = DefSubclassProc(hwnd, message, wParam, lParam);
                if (!HasSystemCaption(hwnd))
                    return hit;
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &point);
                const int border = CanResize(hwnd) ? ResizeBorderHeight(hwnd) : 0;
                if (point.y < border && (hit == HTLEFT || hit == HTRIGHT))
                    return hit == HTLEFT ? HTTOPLEFT : HTTOPRIGHT;
                if (hit != HTCLIENT)
                    return hit;
                RECT client{};
                GetClientRect(hwnd, &client);
                switch (HitTestCaption(state.layout, static_cast<float>(point.x), static_cast<float>(point.y),
                                       static_cast<float>(client.right), static_cast<float>(border)))
                {
                case CaptionHit::Caption: return HTCAPTION;
                case CaptionHit::MaximizeButton: return HTMAXBUTTON;
                case CaptionHit::ResizeTop: return HTTOP;
                case CaptionHit::ResizeTopLeft: return HTTOPLEFT;
                case CaptionHit::ResizeTopRight: return HTTOPRIGHT;
                case CaptionHit::Client: break;
                }
                return HTCLIENT;
            }
            // The maximize button is non-client so that Windows 11 shows snap
            // layouts over it. Its input must not reach DefWindowProc, which
            // would paint a classic caption button.
            case WM_NCMOUSEMOVE:
                state.maximizeHovered = wParam == HTMAXBUTTON;
                if (!state.maximizeHovered)
                    state.maximizePressed = false;
                if (!state.trackingNonClientLeave)
                {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                    state.trackingNonClientLeave = TrackMouseEvent(&track) != FALSE;
                }
                if (state.maximizeHovered)
                    return 0;
                break;
            case WM_NCMOUSELEAVE:
            case WM_MOUSEMOVE:
                if (state.maximizeHovered || state.trackingNonClientLeave)
                    ClearMaximizeState(state);
                break;
            case WM_NCLBUTTONDOWN:
            case WM_NCLBUTTONDBLCLK:
                if (wParam == HTMAXBUTTON)
                {
                    state.maximizePressed = true;
                    return 0;
                }
                break;
            case WM_NCLBUTTONUP:
                if (wParam == HTMAXBUTTON)
                {
                    if (state.maximizePressed)
                        PostMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
                    state.maximizePressed = false;
                    return 0;
                }
                break;
            case WM_NCRBUTTONUP:
                if (wParam == HTCAPTION || wParam == HTMAXBUTTON)
                {
                    ShowSystemMenuAt(hwnd, POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                    return 0;
                }
                break;
            case WM_SYSCHAR:
                // GLFW swallows Alt key menus, including Alt+Space.
                if (wParam == VK_SPACE && HasSystemCaption(hwnd))
                {
                    POINT origin{0, 0};
                    ClientToScreen(hwnd, &origin);
                    origin.y += static_cast<LONG>(state.layout.caption.maxY);
                    ShowSystemMenuAt(hwnd, origin);
                    return 0;
                }
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, FrameProc, kSubclassId);
                state.hwnd = nullptr;
                state.window = nullptr;
                ClearMaximizeState(state);
                break;
            default:
                break;
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        // Window rectangle minus client rectangle, as the offset of the client
        // origin and the extra width and height the frame adds.
        struct FrameExtents
        {
            POINT clientOffset{};
            SIZE frameSize{};
        };

        FrameExtents MeasureFrame(HWND hwnd)
        {
            RECT window{}, client{};
            GetWindowRect(hwnd, &window);
            GetClientRect(hwnd, &client);
            POINT origin{0, 0};
            ClientToScreen(hwnd, &origin);
            return {{origin.x - window.left, origin.y - window.top},
                    {(window.right - window.left) - client.right, (window.bottom - window.top) - client.bottom}};
        }
    }
#endif

    CustomWindowFrame::CustomWindowFrame() : m_state(std::make_unique<State>()) {}

    CustomWindowFrame::~CustomWindowFrame()
    {
        Detach();
    }

    bool CustomWindowFrame::IsSupported()
    {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }

    bool CustomWindowFrame::Attach(GLFWwindow *window)
    {
        Detach();
#ifdef _WIN32
        HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
        if (!hwnd || !SetWindowSubclass(hwnd, FrameProc, kSubclassId, reinterpret_cast<DWORD_PTR>(m_state.get())))
            return false;
        m_state->window = window;
        m_state->hwnd = hwnd;
        // Re-run WM_NCCALCSIZE so the caption area becomes client area now.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        return true;
#else
        static_cast<void>(window);
        return false;
#endif
    }

    void CustomWindowFrame::Detach()
    {
#ifdef _WIN32
        if (m_state->hwnd && IsWindow(m_state->hwnd))
        {
            RemoveWindowSubclass(m_state->hwnd, FrameProc, kSubclassId);
            SetWindowPos(m_state->hwnd, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        m_state->hwnd = nullptr;
        m_state->trackingNonClientLeave = false;
#endif
        m_state->window = nullptr;
        m_state->layout = {};
        m_state->maximizeHovered = false;
        m_state->maximizePressed = false;
    }

    GLFWwindow *CustomWindowFrame::GetWindow() const
    {
        return m_state->window;
    }

    bool CustomWindowFrame::IsActive() const
    {
#ifdef _WIN32
        return m_state->hwnd && HasSystemCaption(m_state->hwnd);
#else
        return false;
#endif
    }

    void CustomWindowFrame::SetCaptionLayout(CaptionLayout layout)
    {
        m_state->layout = std::move(layout);
    }

    void CustomWindowFrame::SetAppearance(const FrameAppearance &appearance)
    {
#ifdef _WIN32
        if (!m_state->hwnd)
            return;
        const BOOL dark = appearance.dark ? TRUE : FALSE;
        DwmSetWindowAttribute(m_state->hwnd, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
        COLORREF border = kDwmColorDefault;
        if (appearance.borderColor)
        {
            const std::uint32_t rgb = *appearance.borderColor;
            border = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
        DwmSetWindowAttribute(m_state->hwnd, kDwmBorderColor, &border, sizeof(border));
#else
        static_cast<void>(appearance);
#endif
    }

    bool CustomWindowFrame::IsMaximizeHovered() const
    {
        return m_state->maximizeHovered;
    }

    bool CustomWindowFrame::IsMaximizePressed() const
    {
        return m_state->maximizePressed;
    }

    bool CustomWindowFrame::IsWindowMaximized() const
    {
        return m_state->window && glfwGetWindowAttrib(m_state->window, GLFW_MAXIMIZED) != 0;
    }

    void CustomWindowFrame::Execute(WindowCommand command)
    {
#ifdef _WIN32
        if (!m_state->hwnd)
            return;
        WPARAM systemCommand = SC_CLOSE;
        switch (command)
        {
        case WindowCommand::Minimize: systemCommand = SC_MINIMIZE; break;
        case WindowCommand::Maximize: systemCommand = SC_MAXIMIZE; break;
        case WindowCommand::Restore: systemCommand = SC_RESTORE; break;
        case WindowCommand::Close: systemCommand = SC_CLOSE; break;
        }
        PostMessageW(m_state->hwnd, WM_SYSCOMMAND, systemCommand, 0);
#else
        static_cast<void>(command);
#endif
    }

    void CustomWindowFrame::ShowSystemMenu(float clientX, float clientY)
    {
#ifdef _WIN32
        if (!m_state->hwnd)
            return;
        POINT point{static_cast<LONG>(clientX), static_cast<LONG>(clientY)};
        ClientToScreen(m_state->hwnd, &point);
        ShowSystemMenuAt(m_state->hwnd, point);
#else
        static_cast<void>(clientX);
        static_cast<void>(clientY);
#endif
    }

    void CustomWindowFrame::SetClientPosition(int x, int y)
    {
#ifdef _WIN32
        if (m_state->hwnd)
        {
            const FrameExtents frame = MeasureFrame(m_state->hwnd);
            SetWindowPos(m_state->hwnd, nullptr, x - frame.clientOffset.x, y - frame.clientOffset.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            return;
        }
#endif
        if (m_state->window)
            glfwSetWindowPos(m_state->window, x, y);
    }

    void CustomWindowFrame::SetClientSize(int width, int height)
    {
#ifdef _WIN32
        if (m_state->hwnd)
        {
            const FrameExtents frame = MeasureFrame(m_state->hwnd);
            SetWindowPos(m_state->hwnd, nullptr, 0, 0, width + frame.frameSize.cx, height + frame.frameSize.cy,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            return;
        }
#endif
        if (m_state->window)
            glfwSetWindowSize(m_state->window, width, height);
    }
}
