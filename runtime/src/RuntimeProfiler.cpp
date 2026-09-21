#include "RuntimeProfiler.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace PlutoGE
{
    RuntimeProfiler::RuntimeProfiler(render::Renderer &renderer)
        : m_panel(ui::PanelConfig{"Runtime Profiler"}, &profiler, nullptr, &renderer) {}

    RuntimeProfiler::~RuntimeProfiler() { Shutdown(); }

    bool RuntimeProfiler::Initialize(bool visible)
    {
        auto *previousWindow = glfwGetCurrentContext();
        auto *previousContext = ImGui::GetCurrentContext();
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        m_window = glfwCreateWindow(1100, 800, "PlutoGE Runtime Profiler", nullptr, nullptr);
        if (!m_window) return false;
        glfwMakeContextCurrent(m_window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            glfwMakeContextCurrent(previousWindow);
            glfwDestroyWindow(m_window);
            m_window = nullptr;
            return false;
        }
        glfwSwapInterval(0);
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        m_glfwBackend = ImGui_ImplGlfw_InitForOpenGL(m_window, true);
        if (m_glfwBackend)
            m_glBackend = ImGui_ImplOpenGL3_Init("#version 330 core");
        ImGui::SetCurrentContext(previousContext);
        glfwMakeContextCurrent(previousWindow);
        if (!m_glBackend)
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void RuntimeProfiler::Draw()
    {
        if (!m_window) return;
        if (glfwWindowShouldClose(m_window))
        {
            Shutdown();
            return;
        }
        auto *previousWindow = glfwGetCurrentContext();
        auto *previousContext = ImGui::GetCurrentContext();
        glfwMakeContextCurrent(m_window);
        ImGui::SetCurrentContext(m_context);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        if (ImGui::Begin("Runtime Profiler", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextDisabled("Game frame timings exclude profiler window rendering.");
            m_panel.Render();
        }
        ImGui::End();
        ImGui::Render();
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width > 0 && height > 0)
        {
            glViewport(0, 0, width, height);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(m_window);
        }
        ImGui::SetCurrentContext(previousContext);
        glfwMakeContextCurrent(previousWindow);
    }

    void RuntimeProfiler::Shutdown()
    {
        if (!m_window) return;
        auto *previousWindow = glfwGetCurrentContext();
        auto *previousContext = ImGui::GetCurrentContext();
        glfwMakeContextCurrent(m_window);
        ImGui::SetCurrentContext(m_context);
        if (m_glBackend) ImGui_ImplOpenGL3_Shutdown();
        if (m_glfwBackend) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_context);
        ImGui::SetCurrentContext(previousContext == m_context ? nullptr : previousContext);
        glfwMakeContextCurrent(previousWindow == m_window ? nullptr : previousWindow);
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        m_context = nullptr;
        m_glBackend = m_glfwBackend = false;
        profiler.StopCapture();
    }
}
