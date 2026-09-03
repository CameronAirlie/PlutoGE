#include "PlutoGE/platform/Window.h"
#include <iostream>

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

    void GLFWFramebufferResizeCallback(GLFWwindow *window, int width, int height)
    {
        auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
        if (!instance)
        {
            return;
        }

        instance->m_clientWidth = width;
        instance->m_clientHeight = height;

        if (auto callback = instance->GetResizeCallback())
        {
            callback(width, height);
        }
    }

    bool Window::Create(const WindowConfig &config)
    {
        m_config = config;
        m_clientWidth = config.width;
        m_clientHeight = config.height;

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

        m_window = glfwCreateWindow(m_clientWidth, m_clientHeight, m_config.title.c_str(), m_config.fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);
        if (!m_window)
        {
            std::cerr << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }

#ifdef _WIN32
        ApplyEmbeddedWindowIcon(static_cast<GLFWwindow *>(m_window));
#endif

        if (m_config.clientApi == WindowClientApi::OpenGL)
            glfwMakeContextCurrent(m_window);
        glfwSetWindowUserPointer(m_window, this);
        glfwGetFramebufferSize(m_window, &m_clientWidth, &m_clientHeight);

        if (!m_config.resizable)
        {
            glfwSetWindowAttrib(m_window, GLFW_RESIZABLE, GLFW_FALSE);
        }
        if (!m_config.visible)
        {
            glfwHideWindow(m_window);
        }
        if (m_config.fullscreen)
        {
            glfwSetWindowMonitor(m_window, glfwGetPrimaryMonitor(), 0, 0, m_clientWidth, m_clientHeight, GLFW_DONT_CARE);
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

        glfwSetWindowCloseCallback(static_cast<GLFWwindow *>(m_window), [](GLFWwindow *window)
                                   {
            auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
            if (instance)
            {
                instance->m_inputState.quitRequested = true;
            } });

        return true;
    }

    void Window::PollEvents()
    {
        m_inputState.BeginFrame();
        glfwPollEvents();
        // Present a compact logical list to scripts. GLFW joystick IDs are physical
        // slots and commonly contain gaps (or virtual/non-gamepad devices), so JID 0
        // is not reliably the player's first controller on Windows.
        std::size_t gamepadIndex = 0;
        for (int joystick = GLFW_JOYSTICK_1;
             joystick <= GLFW_JOYSTICK_LAST && gamepadIndex < m_inputState.gamepads.size();
             ++joystick)
        {
            GLFWgamepadstate state{};
            if (!glfwJoystickIsGamepad(joystick) || !glfwGetGamepadState(joystick, &state))
                continue;

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
    }

    void Window::Close()
    {
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

    const WindowConfig Window::GetConfig() const
    {
        return m_config;
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
            glfwSetInputMode(window, GLFW_CURSOR, m_isCursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

            // Bypass OS cursor acceleration while locked so mouse deltas represent
            // unmodified device motion, as expected by first-person camera controls.
            if (glfwRawMouseMotionSupported())
            {
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, m_isCursorLocked ? GLFW_TRUE : GLFW_FALSE);
            }
        }
    }

    void Window::SetContextCurrent()
    {
        if (m_window && m_config.clientApi == WindowClientApi::OpenGL)
        {
            glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));
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
}
