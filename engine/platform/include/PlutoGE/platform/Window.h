#pragma once

#include <string>
#include <functional>
#include "PlutoGE/platform/InputState.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace PlutoGE::platform
{
    enum class WindowClientApi
    {
        OpenGL,
        None,
    };

    struct WindowConfig
    {
        std::string title = "PlutoGE Window";
        int width = 800;
        int height = 600;
        bool resizable = true;
        bool visible = true;
        bool fullscreen = false;
        WindowClientApi clientApi = WindowClientApi::OpenGL;
        std::function<void(int, int)> resizeCallback = nullptr;
    };

    struct WindowExtents
    {
        int width;
        int height;
    };

    class Window
    {
    public:
        bool Create(const WindowConfig &config);
        void PollEvents();
        void Close();
        void RequestClose();
        void SetTitle(const std::string &title);

        [[nodiscard]] bool IsOpen() const;
        [[nodiscard]] bool ShouldClose() const;
        [[nodiscard]] WindowExtents GetExtents() const;
        [[nodiscard]] const WindowConfig GetConfig() const;
        [[nodiscard]] void *GetWindow() const;
        [[nodiscard]] WindowClientApi GetClientApi() const { return m_config.clientApi; }
        [[nodiscard]] InputState &GetInputState() { return m_inputState; }
        [[nodiscard]] bool IsCursorLocked() const;
        [[nodiscard]] bool IsCursorLockRequested() const { return m_requestedScriptCursorLocked; }
        [[nodiscard]] bool IsCursorLockOverridden() const { return m_forceCursorVisible; }
        [[nodiscard]] bool IsScriptInputEnabled() const { return m_isScriptInputEnabled; }

        void SetResizeCallback(const std::function<void(int, int)> &callback);
        void SetCursorLocked(bool locked);
        void SetEditorCursorLocked(bool locked);
        void SetCursorLockOverride(bool forceVisible);
        void SetScriptInputEnabled(bool enabled);

        void SetContextCurrent();
        bool EnsureOpenGLContextCurrent(bool reloadFunctions = false);

        std::function<void(int, int)> GetResizeCallback() const
        {
            return m_config.resizeCallback;
        }

    private:
        friend void GLFWFramebufferResizeCallback(GLFWwindow *window, int width, int height);

        WindowConfig m_config;
        InputState m_inputState;
        GLFWwindow *m_window = nullptr;
        int m_clientWidth = 0;
        int m_clientHeight = 0;
        bool m_isCursorLocked = false;
        bool m_isScriptInputEnabled = true;
        bool m_requestedScriptCursorLocked = false;
        bool m_requestedEditorCursorLocked = false;
        bool m_forceCursorVisible = false;

        void ApplyCursorMode();
    };
}
