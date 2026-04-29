#include "PlutoGE/platform/Window.h"
#include <iostream>

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
                    }
                    else if (action == GLFW_RELEASE)
                    {
                        instance->m_inputState.keys[key] = false;
                    }
                }
            } });

        return true;
    }

    void Window::PollEvents()
    {
        glfwPollEvents();
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

    void Window::SetResizeCallback(const std::function<void(int, int)> &callback)
    {
        m_config.resizeCallback = callback;
        if (m_window)
        {
            glfwSetFramebufferSizeCallback(static_cast<GLFWwindow *>(m_window), GLFWFramebufferResizeCallback);
        }
    }

    void Window::SetContextCurrent()
    {
        if (m_window)
        {
            glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));
        }
    }
}