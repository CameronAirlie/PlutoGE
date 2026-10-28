#include "PlutoGE/platform/Window.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <unordered_map>
extern "C" int plutoSetJoystickRumble(int, unsigned short, unsigned short);

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include "../../../packaging/branding/PlutoGEResource.h"
#endif

namespace PlutoGE::platform
{
    namespace
    {
#ifdef _WIN32
        void RestoreNativeEventRouting(GLFWwindow *window)
        {
            if (!window) return;
            const HWND nativeWindow = glfwGetWin32Window(window);
            // GLFW's Win32 procedure uses this association before dispatching
            // any event. If a native integration loses the property, cursor
            // polling still updates hover while clicks, keys and resize events
            // disappear. Restore our owned window's association, leaving the
            // actual GLFW/ImGui callback chain intact.
            if (nativeWindow && GetPropW(nativeWindow, L"GLFW") != window)
                SetPropW(nativeWindow, L"GLFW", window);
        }

        void ApplyEmbeddedWindowIcon(GLFWwindow *window)
        {
            if (!window)
            {
                return;
            }

            const HINSTANCE module = GetModuleHandleW(nullptr);
            const HWND nativeWindow = glfwGetWin32Window(window);
            if (!module || !nativeWindow)
            {
                return;
            }

            const int largeWidth = GetSystemMetrics(SM_CXICON);
            const int largeHeight = GetSystemMetrics(SM_CYICON);
            const int smallWidth = GetSystemMetrics(SM_CXSMICON);
            const int smallHeight = GetSystemMetrics(SM_CYSMICON);
            const auto largeIcon = static_cast<HICON>(LoadImageW(module,
                                                                 MAKEINTRESOURCEW(IDI_PLUTOGE_ICON),
                                                                 IMAGE_ICON,
                                                                 largeWidth,
                                                                 largeHeight,
                                                                 LR_DEFAULTCOLOR));
            const auto smallIcon = static_cast<HICON>(LoadImageW(module,
                                                                 MAKEINTRESOURCEW(IDI_PLUTOGE_ICON),
                                                                 IMAGE_ICON,
                                                                 smallWidth,
                                                                 smallHeight,
                                                                 LR_DEFAULTCOLOR));

            if (largeIcon)
            {
                SendMessageW(nativeWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
                SetClassLongPtrW(nativeWindow, GCLP_HICON, reinterpret_cast<LONG_PTR>(largeIcon));
            }
            if (smallIcon)
            {
                SendMessageW(nativeWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
                SetClassLongPtrW(nativeWindow, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(smallIcon));
            }
        }
#endif
    }

    namespace
    {
        // Native windows whose drops are delivered to another Window.
        std::unordered_map<GLFWwindow *, Window *> &DropForwarding()
        {
            static std::unordered_map<GLFWwindow *, Window *> forwarding;
            return forwarding;
        }
    }

    void GLFWDropCallback(GLFWwindow *window, int count, const char **paths)
    {
        const auto forwarded = DropForwarding().find(window);
        auto *instance = forwarded != DropForwarding().end() ? forwarded->second
                                                             : static_cast<Window *>(glfwGetWindowUserPointer(window));
        if (!instance)
        {
            return;
        }
        // GLFW reports UTF-8 paths.
        for (int index = 0; index < count; ++index)
        {
            instance->m_droppedPaths.emplace_back(std::u8string(reinterpret_cast<const char8_t *>(paths[index])));
        }
    }

    void Window::ForwardDropsFrom(GLFWwindow *window)
    {
        if (!window || window == m_window)
        {
            return;
        }
        DropForwarding()[window] = this;
        glfwSetDropCallback(window, GLFWDropCallback);
    }

    void Window::StopForwardingDropsFrom(GLFWwindow *window)
    {
        if (DropForwarding().erase(window) > 0)
        {
            glfwSetDropCallback(window, nullptr);
        }
    }

    void GLFWFramebufferResizeCallback(GLFWwindow *window, int width, int height)
    {
        auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
        if (!instance)
        {
            return;
        }

        instance->m_clientWidth = width;
        instance->m_clientHeight = height;

        instance->m_resizePending = true;
    }

    bool Window::Create(const WindowConfig &config)
    {
        m_config = config;
        m_clientWidth = config.width;
        m_clientHeight = config.height;
        m_resizePending = false;

        if (!glfwInit())
        {
            std::cout << "Failed to initialize GLFW." << std::endl;
            return false;
        }

        glfwDefaultWindowHints();
        if (m_config.clientApi == WindowClientApi::OpenGL)
        {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        }
        else
        {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        m_window = glfwCreateWindow(m_clientWidth, m_clientHeight, m_config.title.c_str(), nullptr, nullptr);
        if (!m_window)
        {
            std::cerr << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }

#ifdef _WIN32
        RestoreNativeEventRouting(static_cast<GLFWwindow *>(m_window));
        ApplyEmbeddedWindowIcon(static_cast<GLFWwindow *>(m_window));
#endif

        if (m_config.customTitleBar && CustomWindowFrame::IsSupported())
        {
            m_customFrame = std::make_unique<CustomWindowFrame>();
            if (!m_customFrame->Attach(m_window))
                m_customFrame.reset();
        }

        if (m_config.clientApi == WindowClientApi::OpenGL)
            glfwMakeContextCurrent(m_window);
        glfwSetWindowUserPointer(m_window, this);
        glfwGetFramebufferSize(m_window, &m_clientWidth, &m_clientHeight);
        glfwGetCursorPos(m_window, &m_inputState.mouseState.x, &m_inputState.mouseState.y);

        if (!m_config.resizable)
        {
            glfwSetWindowAttrib(m_window, GLFW_RESIZABLE, GLFW_FALSE);
        }
        if (m_config.minWidth > 0 || m_config.minHeight > 0)
        {
            glfwSetWindowSizeLimits(m_window,
                                    m_config.minWidth > 0 ? m_config.minWidth : GLFW_DONT_CARE,
                                    m_config.minHeight > 0 ? m_config.minHeight : GLFW_DONT_CARE,
                                    GLFW_DONT_CARE, GLFW_DONT_CARE);
        }
        if (!m_config.visible)
        {
            glfwHideWindow(m_window);
        }
        if (m_config.fullscreen)
        {
            m_config.fullscreen = false;
            SetFullscreen(true);
        }
        if (m_config.resizeCallback)
        {
            SetResizeCallback(config.resizeCallback);
        }

        glfwSetKeyCallback(static_cast<GLFWwindow *>(m_window), [](GLFWwindow *window, int key, int scancode, int action, int mods)
                           {
            auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
            if (instance)
            {
                // Update input state based on key events
                if (key >= 0 && key < static_cast<int>(instance->m_inputState.keys.size()))
                {
                    if (action == GLFW_PRESS)
                    {
                        instance->m_inputState.keys[key] = true;
                        if (key == GLFW_KEY_ESCAPE)
                        {
                            instance->m_inputState.escapePressed = true;
                        }
                    }
                    else if (action == GLFW_REPEAT)
                    {
                        instance->m_inputState.repeatedKeys.push_back(key);
                    }
                    else if (action == GLFW_RELEASE)
                    {
                        instance->m_inputState.keys[key] = false;
                    }
                }
            } });

        glfwSetCharCallback(static_cast<GLFWwindow *>(m_window), [](GLFWwindow *window, unsigned int codepoint)
                            {
            auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
            if (instance)
                instance->m_inputState.textInput.push_back(codepoint); });

        glfwSetMouseButtonCallback(static_cast<GLFWwindow *>(m_window), [](GLFWwindow *window, int button, int action, int mods)
                                   {
            auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
            if (!instance || button < 0 || button >= 8)
            {
                return;
            }

            if (action == GLFW_PRESS)
            {
                instance->m_inputState.mouseState.buttons[button] = true;
            }
            else if (action == GLFW_RELEASE)
            {
                instance->m_inputState.mouseState.buttons[button] = false;
            } });

        glfwSetCursorPosCallback(static_cast<GLFWwindow *>(m_window), [](GLFWwindow *window, double x, double y)
                                 {
            auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
            if (!instance)
            {
                return;
            }

            instance->m_inputState.mouseState.deltaX += x - instance->m_inputState.mouseState.x;
            instance->m_inputState.mouseState.deltaY += y - instance->m_inputState.mouseState.y;
            instance->m_inputState.mouseState.x = x;
            instance->m_inputState.mouseState.y = y; });

        glfwSetScrollCallback(static_cast<GLFWwindow *>(m_window), [](GLFWwindow *window, double xOffset, double yOffset)
                              {
            auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
            if (!instance)
            {
                return;
            }

            instance->m_inputState.mouseState.scrollDeltaX += xOffset;
            instance->m_inputState.mouseState.scrollDeltaY += yOffset; });

        glfwSetDropCallback(static_cast<GLFWwindow *>(m_window), GLFWDropCallback);

        glfwSetWindowCloseCallback(static_cast<GLFWwindow *>(m_window), [](GLFWwindow *window)
                                   {
            auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
            if (instance)
            {
                instance->m_inputState.quitRequested = true;
            } });

        return true;
    }

    bool Window::SetGamepadVibration(int gamepad, float low, float high, float seconds)
    {
        if (!m_window || gamepad < 0 || gamepad >= static_cast<int>(MaxGamepads) ||
            !std::isfinite(low) || !std::isfinite(high) || !std::isfinite(seconds) ||
            low < 0 || low > 1 || high < 0 || high > 1 || seconds < 0 || seconds > 5) return false;
        const int jid = m_gamepadJoysticks[gamepad];
        if (jid < 0) return false;
        const bool stop = seconds == 0 || (low == 0 && high == 0);
        if (!stop && (!m_isScriptInputEnabled || !glfwGetWindowAttrib(m_window, GLFW_FOCUSED))) return false;
        const bool result = plutoSetJoystickRumble(jid, stop ? 0 : static_cast<unsigned short>(low * 65535),
            stop ? 0 : static_cast<unsigned short>(high * 65535)) != 0;
        m_rumbleUntil[jid] = result && !stop ? glfwGetTime() + seconds : 0;
        return result;
    }

    void Window::StopGamepadVibration()
    {
        for (int jid = 0; jid < static_cast<int>(m_rumbleUntil.size()); ++jid)
            if (m_rumbleUntil[jid] > 0) { plutoSetJoystickRumble(jid, 0, 0); m_rumbleUntil[jid] = 0; }
    }

    void Window::PollEvents()
    {
#ifdef _WIN32
        RestoreNativeEventRouting(static_cast<GLFWwindow *>(m_window));
#endif
        m_inputState.BeginFrame();
        m_droppedPaths.clear();
        glfwPollEvents();
        // The framebuffer is authoritative even when a native resize notification
        // was missed or another integration replaced GLFW's callback. Keep the
        // presentation extent consistent with the size ImGui queries each frame.
        if (m_window)
        {
            const auto extent = GetExtents();
            if (extent.width != m_clientWidth || extent.height != m_clientHeight)
            {
                m_clientWidth = extent.width;
                m_clientHeight = extent.height;
                m_resizePending = true;
            }
        }
        // A drag can deliver many sizes in one poll. Rebuild GPU resources only
        // for the latest size, outside GLFW's C callback stack.
        if (m_resizePending)
        {
            m_resizePending = false;
            if (auto callback = GetResizeCallback())
                callback(m_clientWidth, m_clientHeight);
        }
        // Present a compact logical list to scripts. GLFW joystick IDs are physical
        // slots and commonly contain gaps (or virtual/non-gamepad devices), so JID 0
        // is not reliably the player's first controller on Windows.
        const auto previousJoysticks = m_gamepadJoysticks;
        m_gamepadJoysticks.fill(-1);
        std::size_t gamepadIndex = 0;
        for (int joystick = GLFW_JOYSTICK_1;
             joystick <= GLFW_JOYSTICK_LAST && gamepadIndex < m_inputState.gamepads.size();
             ++joystick)
        {
            GLFWgamepadstate state{};
            if (!glfwJoystickIsGamepad(joystick) || !glfwGetGamepadState(joystick, &state))
                continue;

            m_gamepadJoysticks[gamepadIndex] = joystick;
            auto &destination = m_inputState.gamepads[gamepadIndex++];
            destination.connected = true;
            for (std::size_t button = 0; button < destination.buttons.size(); ++button)
                destination.buttons[button] = state.buttons[button] == GLFW_PRESS;
            for (std::size_t axis = 0; axis < destination.axes.size(); ++axis)
                destination.axes[axis] = state.axes[axis];
            // Present triggers as the conventional 0..1 range (GLFW reports -1..1).
            destination.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = (destination.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
            destination.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = (destination.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
        }
        // Some otherwise usable DirectInput controllers have no GLFW mapping and
        // therefore fail glfwJoystickIsGamepad(). Keep them available with their
        // native button/axis order instead of silently ignoring them.
        for (int joystick = GLFW_JOYSTICK_1;
             joystick <= GLFW_JOYSTICK_LAST && gamepadIndex < m_inputState.gamepads.size();
             ++joystick)
        {
            if (!glfwJoystickPresent(joystick) || glfwJoystickIsGamepad(joystick)) continue;
            int buttonCount = 0, axisCount = 0;
            const unsigned char *buttons = glfwGetJoystickButtons(joystick, &buttonCount);
            const float *axes = glfwGetJoystickAxes(joystick, &axisCount);
            if ((!buttons || buttonCount == 0) && (!axes || axisCount == 0)) continue;
            m_gamepadJoysticks[gamepadIndex] = joystick;
            auto &destination = m_inputState.gamepads[gamepadIndex++];
            destination.connected = true;
            destination.buttons.fill(false);
            destination.axes.fill(0.0f);
            for (int button = 0; button < buttonCount && button < static_cast<int>(destination.buttons.size()); ++button)
                destination.buttons[button] = buttons[button] == GLFW_PRESS;
            for (int axis = 0; axis < axisCount && axis < static_cast<int>(destination.axes.size()); ++axis)
                destination.axes[axis] = axes[axis];
        }
        for (; gamepadIndex < m_inputState.gamepads.size(); ++gamepadIndex)
        {
            auto &destination = m_inputState.gamepads[gamepadIndex];
            destination.connected = false;
            destination.buttons.fill(false);
            destination.axes.fill(0.0f);
        }
        if (previousJoysticks != m_gamepadJoysticks) StopGamepadVibration();
        for (int jid = 0; jid < static_cast<int>(m_rumbleUntil.size()); ++jid)
            if (m_rumbleUntil[jid] > 0 && (glfwGetTime() >= m_rumbleUntil[jid] ||
                !m_isScriptInputEnabled || !glfwGetWindowAttrib(m_window, GLFW_FOCUSED)))
            { plutoSetJoystickRumble(jid, 0, 0); m_rumbleUntil[jid] = 0; }
        m_inputState.UpdateActiveDevice();
    }

    void Window::Close()
    {
        StopGamepadVibration();
        std::erase_if(DropForwarding(), [this](const auto &entry) { return entry.second == this; });
        m_customFrame.reset();
        if (m_window)
        {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
#ifndef _WIN32
        glfwTerminate();
#else
        // GLFW's hidden Win32 helper window receives joystick WM_DEVICECHANGE
        // notifications. Destroying it during glfwTerminate can re-enter the
        // joystick detector after DirectInput teardown has begun, causing an
        // access violation on editor exit. Keep GLFW's process-wide Win32
        // state alive until process termination; actual engine windows and
        // their graphics contexts are still destroyed above. glfwInit is
        // idempotent, so graphics-backend restarts safely reuse this state.
#endif
    }

    void Window::RequestClose()
    {
        if (m_window)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
    }

    void Window::SetFullscreen(bool fullscreen)
    {
        if (!m_window || m_config.fullscreen == fullscreen) return;
        if (fullscreen)
        {
            glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
            glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);
            m_windowedMaximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0;
            GLFWmonitor *selected = glfwGetPrimaryMonitor();
            int count = 0, bestArea = -1;
            GLFWmonitor **monitors = glfwGetMonitors(&count);
            for (int i = 0; i < count; ++i)
            {
                int x, y;
                glfwGetMonitorPos(monitors[i], &x, &y);
                const auto *mode = glfwGetVideoMode(monitors[i]);
                if (!mode) continue;
                const int area = (std::max)(0, (std::min)(m_windowedX + m_windowedWidth, x + mode->width) - (std::max)(m_windowedX, x)) *
                                 (std::max)(0, (std::min)(m_windowedY + m_windowedHeight, y + mode->height) - (std::max)(m_windowedY, y));
                if (area > bestArea) { selected = monitors[i]; bestArea = area; }
            }
            const auto *mode = selected ? glfwGetVideoMode(selected) : nullptr;
            if (!mode) return;
            int x, y;
            glfwGetMonitorPos(selected, &x, &y);
            if (m_windowedMaximized)
            {
                glfwRestoreWindow(m_window);
                glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
                glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);
            }
            glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_FALSE);
            // A monitor-less window covers the desktop without an exclusive video-mode switch.
            glfwSetWindowMonitor(m_window, nullptr, x, y, mode->width, mode->height, GLFW_DONT_CARE);
        }
        else
        {
            glfwSetWindowAttrib(m_window, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(m_window, nullptr, m_windowedX, m_windowedY, m_windowedWidth, m_windowedHeight, GLFW_DONT_CARE);
            // GLFW sizes the restored frame for a system caption.
            if (m_customFrame && m_customFrame->IsActive())
            {
                m_customFrame->SetClientPosition(m_windowedX, m_windowedY);
                m_customFrame->SetClientSize(m_windowedWidth, m_windowedHeight);
            }
            if (m_windowedMaximized) glfwMaximizeWindow(m_window);
        }
        m_config.fullscreen = fullscreen;
        glfwGetFramebufferSize(m_window, &m_clientWidth, &m_clientHeight);
    }

    WindowExtents Window::GetWindowedSize() const
    {
        if (!m_window) return {0, 0};
        if (m_config.fullscreen) return {m_windowedWidth, m_windowedHeight};
        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        return {width, height};
    }

    WindowExtents Window::GetWindowedSizeLimit() const
    {
        if (!m_window) return {0, 0};
        int x, y, width, height, count;
        glfwGetWindowPos(m_window, &x, &y);
        glfwGetWindowSize(m_window, &width, &height);
        auto *selected = glfwGetPrimaryMonitor();
        auto **monitors = glfwGetMonitors(&count);
        int bestArea = -1;
        for (int i = 0; i < count; ++i)
        {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
            int area = (std::max)(0, (std::min)(x + width, mx + mw) - (std::max)(x, mx)) *
                       (std::max)(0, (std::min)(y + height, my + mh) - (std::max)(y, my));
            if (area > bestArea) { bestArea = area; selected = monitors[i]; }
        }
        if (!selected) return {0, 0};
        glfwGetMonitorWorkarea(selected, &x, &y, &width, &height);
        // Reserve decoration space even while the current window is borderless.
        int left, top, right, bottom;
        glfwGetWindowFrameSize(m_window, &left, &top, &right, &bottom);
        return {(std::max)(0, width - (std::max)(left + right, 16)),
                (std::max)(0, height - (std::max)(top + bottom, 48))};
    }

    bool Window::SetWindowedSize(int width, int height)
    {
        if (!m_window || width < 320 || height < 200) return false;
        const auto current = GetWindowedSize();
        if (current.width == width && current.height == height) return true;
        const auto limit = GetWindowedSizeLimit();
        if (width > limit.width || height > limit.height) return false;
        if (m_config.fullscreen)
        {
            m_windowedWidth = width; m_windowedHeight = height; m_windowedMaximized = false;
            return true;
        }
        if (glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED)) glfwRestoreWindow(m_window);
        if (m_customFrame && m_customFrame->IsActive())
            m_customFrame->SetClientSize(width, height);
        else
            glfwSetWindowSize(m_window, width, height);
        const auto actual = GetWindowedSize();
        return actual.width == width && actual.height == height;
    }

    void Window::SetTitle(const std::string &title)
    {
        m_config.title = title;
        if (m_window)
        {
            glfwSetWindowTitle(m_window, title.c_str());
        }
    }

    bool Window::IsOpen() const
    {
        return m_window != nullptr;
    }

    bool Window::ShouldClose() const
    {
        return m_window ? glfwWindowShouldClose(m_window) : true;
    }

    WindowExtents Window::GetExtents() const
    {
        if (m_window)
        {
            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(m_window, &width, &height);
            return {width, height};
        }
        return {m_clientWidth, m_clientHeight};
    }

    void *Window::GetWindow() const
    {
        return m_window;
    }

    void Window::SetResizeCallback(const std::function<void(int, int)> &callback)
    {
        m_config.resizeCallback = callback;
        if (m_window)
        {
            glfwSetFramebufferSizeCallback(static_cast<GLFWwindow *>(m_window), GLFWFramebufferResizeCallback);
        }
    }

    bool Window::IsCursorLocked() const
    {
        return m_isCursorLocked;
    }

    void Window::SetCursorLocked(bool locked)
    {
        m_requestedScriptCursorLocked = locked;
        ApplyCursorMode();
    }

    void Window::SetEditorCursorLocked(bool locked)
    {
        if (m_requestedEditorCursorLocked == locked)
        {
            return;
        }

        m_requestedEditorCursorLocked = locked;
        ApplyCursorMode();
    }

    void Window::SetCursorLockOverride(bool forceVisible)
    {
        if (m_forceCursorVisible == forceVisible)
        {
            return;
        }
        m_forceCursorVisible = forceVisible;
        ApplyCursorMode();
    }

    void Window::SetScriptInputEnabled(bool enabled)
    {
        if (m_isScriptInputEnabled == enabled)
        {
            return;
        }

        m_isScriptInputEnabled = enabled;
        if (!enabled) StopGamepadVibration();
        if (!enabled)
        {
            m_inputState.ClearKeyStates();
        }
        else if (m_window)
        {
            // Editor viewport focus can disable script input for a frame. Key
            // callbacks do not emit a second press event for keys that remain
            // physically held when gameplay input is enabled again, so rebuild
            // the current state from GLFW. Mirror it into previous state to
            // resume IsKeyDown immediately without creating false Pressed edges.
            auto *window = static_cast<GLFWwindow *>(m_window);
            for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
            {
                const bool down = glfwGetKey(window, key) == GLFW_PRESS;
                m_inputState.keys[static_cast<std::size_t>(key)] = down;
                m_inputState.previousKeys[static_cast<std::size_t>(key)] = down;
            }
            for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button)
            {
                const bool down = glfwGetMouseButton(window, button) == GLFW_PRESS;
                m_inputState.mouseState.buttons[button] = down;
                m_inputState.mouseState.previousButtons[button] = down;
            }
        }

        ApplyCursorMode();
    }

    void Window::ApplyCursorMode()
    {
        const bool scriptLockActive = m_isScriptInputEnabled && m_requestedScriptCursorLocked;
        m_isCursorLocked = (scriptLockActive || m_requestedEditorCursorLocked) && !m_forceCursorVisible;
        if (m_window)
        {
            auto *window = static_cast<GLFWwindow *>(m_window);
            const int cursorMode = m_isCursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
            const bool modeChanged = glfwGetInputMode(window, GLFW_CURSOR) != cursorMode;
            glfwSetInputMode(window, GLFW_CURSOR, cursorMode);

            // Bypass OS cursor acceleration while locked so mouse deltas represent
            // unmodified device motion, as expected by first-person camera controls.
            if (glfwRawMouseMotionSupported())
            {
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, m_isCursorLocked ? GLFW_TRUE : GLFW_FALSE);
            }
            if (modeChanged)
            {
                // GLFW resets its virtual position when entering disabled mode
                // and restores the desktop cursor on release. Neither is mouse
                // motion: rebase before the next callback and discard deltas
                // collected in the previous coordinate system.
                glfwGetCursorPos(window, &m_inputState.mouseState.x, &m_inputState.mouseState.y);
                m_inputState.mouseState.deltaX = 0.0;
                m_inputState.mouseState.deltaY = 0.0;
            }
        }
    }

    bool Window::EnsureOpenGLContextCurrent(bool reloadFunctions)
    {
        if (!m_window || m_config.clientApi != WindowClientApi::OpenGL)
        {
            return false;
        }

        glfwMakeContextCurrent(m_window);
        if (glfwGetCurrentContext() != m_window)
        {
            return false;
        }

        const bool textureDispatchReady =
            glad_glGenTextures != nullptr &&
            glad_glDeleteTextures != nullptr &&
            glad_glBindTexture != nullptr &&
            glad_glTexImage2D != nullptr &&
            glad_glTexStorage2D != nullptr &&
            glad_glTexSubImage2D != nullptr &&
            glad_glTexParameteri != nullptr &&
            glad_glBindBuffer != nullptr &&
            glad_glPixelStorei != nullptr &&
            glad_glGenerateMipmap != nullptr;

        if (reloadFunctions || !GLAD_GL_VERSION_4_3 || !textureDispatchReady)
        {
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            {
                return false;
            }
        }

        return GLAD_GL_VERSION_4_3 &&
               glad_glGenTextures != nullptr &&
               glad_glDeleteTextures != nullptr &&
               glad_glBindTexture != nullptr &&
               glad_glTexImage2D != nullptr &&
               glad_glTexStorage2D != nullptr &&
               glad_glTexSubImage2D != nullptr &&
               glad_glTexParameteri != nullptr &&
               glad_glBindBuffer != nullptr &&
               glad_glPixelStorei != nullptr &&
               glad_glGenerateMipmap != nullptr;
    }

    std::string Window::GetClipboardText() const
    {
        const char *text = m_window ? glfwGetClipboardString(m_window) : nullptr;
        return text ? std::string(text) : std::string{};
    }

    void Window::SetClipboardText(const std::string &text) const
    {
        if (m_window)
            glfwSetClipboardString(m_window, text.c_str());
    }
}
