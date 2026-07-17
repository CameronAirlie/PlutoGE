#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "PlutoGE/platform/Window.h"
#include <iostream>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace PlutoGE::platform
{
    static void GLFWFramebufferResizeCallback(GLFWwindow *window, int width, int height)
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

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, m_config.visible ? GLFW_TRUE : GLFW_FALSE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

        m_window = glfwCreateWindow(m_clientWidth, m_clientHeight, m_config.title.c_str(), m_config.fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);
        if (!m_window)
        {
            std::cerr << "Failed to create GLFW window." << std::endl;
            glfwTerminate();
            return false;
        }

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

#ifdef _WIN32
        if (m_config.embedded && m_config.nativeParent)
        {
            auto *nativeWindow = glfwGetWin32Window(m_window);
            auto *nativeParent = static_cast<HWND>(m_config.nativeParent);
            if (!nativeWindow || !nativeParent)
            {
                std::cerr << "Failed to resolve native handles for embedded window." << std::endl;
                Close();
                return false;
            }

            LONG_PTR style = GetWindowLongPtrW(nativeWindow, GWL_STYLE);
            style &= ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
            style |= WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
            SetWindowLongPtrW(nativeWindow, GWL_STYLE, style);

            LONG_PTR extendedStyle = GetWindowLongPtrW(nativeWindow, GWL_EXSTYLE);
            extendedStyle &= ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);
            // Keep the overlay out of the taskbar, but allow it to receive
            // focus when the user clicks the viewport so GLFW gets WASD/QE.
            extendedStyle |= WS_EX_TOOLWINDOW;
            SetWindowLongPtrW(nativeWindow, GWL_EXSTYLE, extendedStyle);

            // A WS_CHILD window is composited underneath Electron's Chromium
            // surface even when Win32 reports it at the top of the child
            // z-order. Make the engine surface an owned top-level overlay so
            // OpenGL remains GPU-native and is presented above Chromium.
            SetLastError(ERROR_SUCCESS);
            const auto previousOwner = SetWindowLongPtrW(nativeWindow,
                                                         GWLP_HWNDPARENT,
                                                         reinterpret_cast<LONG_PTR>(nativeParent));
            if (!previousOwner && GetLastError() != ERROR_SUCCESS)
            {
                std::cerr << "Failed to attach the engine window to its native owner." << std::endl;
                Close();
                return false;
            }

            POINT parentOrigin{0, 0};
            if (!ClientToScreen(nativeParent, &parentOrigin))
            {
                std::cerr << "Failed to resolve the native owner client origin." << std::endl;
                Close();
                return false;
            }

            UINT positionFlags = SWP_FRAMECHANGED | SWP_NOACTIVATE;
            positionFlags |= m_config.visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
            SetWindowPos(nativeWindow,
                         HWND_TOP,
                         parentOrigin.x,
                         parentOrigin.y,
                         m_clientWidth,
                         m_clientHeight,
                         positionFlags);
        }
#endif
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
                    else if (action == GLFW_RELEASE)
                    {
                        instance->m_inputState.keys[key] = false;
                    }
                }
            } });

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
    }

    void Window::PollEmbeddedEvents()
    {
        m_inputState.BeginFrame();
#ifdef _WIN32
        // Pump the owned overlay's messages without GLFW's global cursor
        // recentering pass. A second embedded GLFW process can otherwise act
        // on cursor capture owned by the interactive Scene View and dereference
        // stale Win32 cursor-window state.
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        // GLFW normally performs this after dispatching all messages through
        // process-global cursor state. Embedded overlays can transfer focus to
        // an owned window in another process, making that global state stale.
        // Recenter only this Window's known-valid GLFW handle instead.
        if (m_isCursorLocked && m_window)
        {
            int width = 0;
            int height = 0;
            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetWindowSize(m_window, &width, &height);
            glfwGetCursorPos(m_window, &cursorX, &cursorY);
            if (static_cast<int>(cursorX) != width / 2 || static_cast<int>(cursorY) != height / 2)
            {
                glfwSetCursorPos(m_window, width / 2.0, height / 2.0);
            }
        }
#else
        glfwPollEvents();
#endif
    }

    void Window::Close()
    {
        if (m_window)
        {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        glfwTerminate();
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

    void *Window::GetNativeHandle() const
    {
#ifdef _WIN32
        return m_window ? static_cast<void *>(glfwGetWin32Window(m_window)) : nullptr;
#else
        return nullptr;
#endif
    }

    bool Window::SetEmbeddedBounds(int x, int y, int width, int height)
    {
        if (!m_window || width <= 0 || height <= 0)
        {
            return false;
        }

        m_clientWidth = width;
        m_clientHeight = height;

#ifdef _WIN32
        if (m_config.embedded)
        {
            auto *nativeWindow = glfwGetWin32Window(m_window);
            auto *nativeParent = static_cast<HWND>(m_config.nativeParent);
            POINT parentOrigin{0, 0};
            if (!nativeWindow || !nativeParent || !ClientToScreen(nativeParent, &parentOrigin))
            {
                return false;
            }
            return nativeWindow && SetWindowPos(nativeWindow,
                                                HWND_TOP,
                                                parentOrigin.x + x,
                                                parentOrigin.y + y,
                                                width,
                                                height,
                                                SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
        }
#endif

        glfwSetWindowPos(m_window, x, y);
        glfwSetWindowSize(m_window, width, height);
        return true;
    }

    void Window::SetEmbeddedVisible(bool visible)
    {
        if (!m_window)
        {
            return;
        }

#ifdef _WIN32
        if (m_config.embedded)
        {
            if (auto *nativeWindow = glfwGetWin32Window(m_window))
            {
                ShowWindow(nativeWindow, visible ? SW_SHOWNA : SW_HIDE);
            }
            return;
        }
#endif

        if (visible)
        {
            glfwShowWindow(m_window);
        }
        else
        {
            glfwHideWindow(m_window);
        }
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

        ApplyCursorMode();
    }

    void Window::ApplyCursorMode()
    {
        const bool scriptLockActive = m_isScriptInputEnabled && m_requestedScriptCursorLocked;
        m_isCursorLocked = (scriptLockActive || m_requestedEditorCursorLocked) && !m_forceCursorVisible;
        if (m_window)
        {
            glfwSetInputMode(static_cast<GLFWwindow *>(m_window), GLFW_CURSOR, m_isCursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
    }

    void Window::SetContextCurrent()
    {
        if (m_window)
        {
            glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));
        }
    }

    bool Window::EnsureOpenGLContextCurrent(bool reloadFunctions)
    {
        if (!m_window)
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
