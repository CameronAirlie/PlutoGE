#include "PlutoGE/platform/Window.h"
#include <iostream>

namespace PlutoGE::platform
{
    // Add a member to store the resize callback
    std::function<void(int, int)> m_resizeCallback;

    // Static GLFW resize callback
    static void GLFWResizeCallback(GLFWwindow *window, int width, int height)
    {
        // Retrieve the Window instance from the user pointer
        std::cout << "GLFWResizeCallback called with width: " << width << ", height: " << height << std::endl;
        auto *instance = static_cast<Window *>(glfwGetWindowUserPointer(window));
        if (auto callback = instance->GetResizeCallback())
        {
            std::cout << "Invoking resize callback from GLFWResizeCallback." << std::endl;
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
        // Set the user pointer to this Window instance
        glfwSetWindowUserPointer(m_window, this);

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
        std::cout << "Setting resize callback in Window class." << std::endl;
        m_config.resizeCallback = callback;
        if (m_window)
        {
            std::cout << "Registering GLFW resize callback." << std::endl;
            glfwSetWindowSizeCallback(static_cast<GLFWwindow *>(m_window), GLFWResizeCallback);
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