#pragma once

#include <string>
#include <functional>
#include "PlutoGE/platform/InputState.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace PlutoGE::platform
{
    struct WindowConfig
    {
        std::string title = "PlutoGE Window";
        int width = 800;
        int height = 600;
        bool resizable = true;
        bool visible = true;
        bool fullscreen = false;
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
        void SetTitle(const std::string &title);

        [[nodiscard]] bool IsOpen() const;
        [[nodiscard]] bool ShouldClose() const;
        [[nodiscard]] WindowExtents GetExtents() const;
        [[nodiscard]] const WindowConfig GetConfig() const;
        [[nodiscard]] void *GetWindow() const;
        [[nodiscard]] InputState &GetInputState() { return m_inputState; }

        void SetResizeCallback(const std::function<void(int, int)> &callback);

        void SetContextCurrent();

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
    };
}