#include "RuntimeProfiler.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>

int main()
{
    if (!glfwInit()) return 77;
    bool valid = true;
    PlutoGE::render::Renderer renderer;
    // Vulkan runtimes have no current OpenGL or ImGui context.
    {
        PlutoGE::RuntimeProfiler window(renderer);
        if (!window.Initialize(false)) { glfwTerminate(); return 77; }
        window.profiler.AddFrameSample(16.0f);
        window.Draw();
        valid = valid && !glfwGetCurrentContext() && !ImGui::GetCurrentContext();
        window.Shutdown();
        window.Shutdown();
        valid = valid && !glfwGetCurrentContext() && !ImGui::GetCurrentContext();
    }
    // OpenGL games must retain their own context after profiler rendering.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *game = glfwCreateWindow(64, 64, "Test game", nullptr, nullptr);
    if (!game) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(game);
    auto *context = ImGui::CreateContext();
    {
        PlutoGE::RuntimeProfiler window(renderer);
        valid = window.Initialize(false) && valid;
        valid = valid && glfwGetCurrentContext() == game && ImGui::GetCurrentContext() == context;
        window.profiler.StartCapture(1);
        PlutoGE::ui::EditorProfileFrame frame;
        frame.durationMs = 16.0f;
        window.profiler.RecordFrame(std::move(frame));
        window.Draw();
        valid = valid && glfwGetCurrentContext() == game && ImGui::GetCurrentContext() == context;
        window.Shutdown();
        valid = valid && glfwGetCurrentContext() == game && ImGui::GetCurrentContext() == context;
    }
    ImGui::DestroyContext(context);
    glfwDestroyWindow(game);
    glfwTerminate();
    std::cout << (valid ? "Runtime profiler context isolation passed\n" : "Runtime profiler context isolation failed\n");
    return valid ? 0 : 1;
}
