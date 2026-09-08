#include "PlutoGE/ui/panels/ProfilerPanel.h"
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cmath>
#include <fstream>
#include <iostream>

// A hidden OpenGL window exercises the real panel and can emit a visual QA fixture.
// It does not start an engine, open a project, or modify editor preferences.
int main(int argc, char **argv)
{
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto *window = glfwCreateWindow(1280, 850, "Profiler UI test", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) return 1;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 330");
    PlutoGE::ui::EditorProfiler profiler;
    PlutoGE::ui::PanelManager manager;
    PlutoGE::render::Renderer renderer;
    PlutoGE::ui::ProfilerPanel panel({"Profiler"}, &profiler, &manager, &renderer);
    profiler.StartCapture(160);
    for (int i = 0; i < 160; ++i)
    {
        using PlutoGE::core::CpuCategory;
        PlutoGE::ui::EditorProfileFrame frame;
        frame.sequence = 1000 + i;
        frame.durationMs = 18.0f + 4 * std::sin(i * 0.3f) + (i % 17 == 0 ? 15.0f : 0);
        if (i == 159) frame.durationMs = 32.56f;
        auto sample = [&](const char *name, float start, float duration, int parent, int depth, CpuCategory category, const char *context = "")
        { frame.samples.push_back({name, context, start, duration, parent, depth, category}); return static_cast<int>(frame.samples.size()) - 1; };
        sample("EditorLoop", 0, frame.durationMs, -1, 0, CpuCategory::Other);
        const int scene = sample("Scene.Update", 0.3f, 7.9f, 0, 1, CpuCategory::Other);
        const int scripts = sample("Component.Update", 0.5f, 5.0f, scene, 2, CpuCategory::Other);
        for (int call = 0; call < 18; ++call)
        {
            const float start = 0.6f + call * 0.26f;
            const int script = sample("PlayerController.Update", start, 0.23f, scripts, 3, CpuCategory::Scripts, "Player");
            sample("Movement", start + 0.01f, 0.09f, script, 4, CpuCategory::Scripts);
            sample("Animation.Update", start + 0.12f, 0.08f, script, 4, CpuCategory::Animation);
        }
        sample("Physics.FixedStep", 5.6f, 1.8f, scene, 2, CpuCategory::Physics);
        sample("Audio.Update", 7.5f, 0.5f, scene, 2, CpuCategory::Audio);
        const float renderMs = frame.durationMs - 12;
        const int render = sample("Viewport.Render", 8.5f, renderMs, 0, 1, CpuCategory::Rendering);
        sample("Shadows", 8.6f, renderMs * 0.35f, render, 2, CpuCategory::Rendering);
        const int geometry = sample("Geometry", 8.7f + renderMs * 0.35f, renderMs * 0.4f, render, 2, CpuCategory::Rendering);
        for (int batch = 0; batch < 12; ++batch)
            sample("Draw indexed", 8.8f + renderMs * 0.35f + batch * renderMs * 0.03f, renderMs * 0.02f, geometry, 3, CpuCategory::Rendering);
        sample("Post processing", 8.8f + renderMs * 0.75f, renderMs * 0.20f, render, 2, CpuCategory::Rendering);
        const int ui = sample("Editor.UI", frame.durationMs - 3.2f, 1.6f, 0, 1, CpuCategory::UI);
        sample("Profiler", frame.durationMs - 3.1f, 0.7f, ui, 2, CpuCategory::UI);
        sample("Inspector", frame.durationMs - 2.3f, 0.5f, ui, 2, CpuCategory::UI);
        sample("Present / VSync", frame.durationMs - 1.5f, 1.4f, 0, 1, CpuCategory::Wait);
        profiler.RecordFrame(std::move(frame));
    }
    auto renderPanel = [&](int width)
    {
        ImGui_ImplOpenGL3_NewFrame();
        // Deterministic synthetic input must not be overwritten by the hidden
        // native window's cursor/focus state.
        ImGui::GetIO().DisplaySize = ImVec2(1280, 850);
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), 850));
        ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        panel.Render();
        ImGui::End();
        ImGui::Render();
        glViewport(0, 0, 1280, 850);
        glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return ImGui::GetDrawData()->TotalVtxCount > 0;
    };
    bool valid = renderPanel(420) && renderPanel(1280) && renderPanel(1280);
    const auto click = [&](float x, float y)
    {
        ImGui::GetIO().AddMousePosEvent(x, y);
        valid = renderPanel(1280) && valid;
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        valid = renderPanel(1280) && valid;
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        valid = renderPanel(1280) && valid;
        valid = renderPanel(1280) && valid;
    };
    const std::string_view mode = argc > 2 ? argv[2] : "timeline";
    if (mode == "hierarchy" || mode == "search")
    {
        click(110, 323);
        if (mode == "search")
        {
            click(210, 346);
            ImGui::GetIO().AddInputCharactersUTF8("Player");
            valid = renderPanel(1280) && valid;
        }
    }
    else if (mode == "focus")
    {
        click(500, 427);
        click(125, 346);
    }
    else if (mode == "narrow") valid = renderPanel(420) && renderPanel(420) && valid;
    if (argc > 1)
    {
        std::vector<unsigned char> pixels(1280 * 850 * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 1280, 850, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream file(argv[1], std::ios::binary);
        file << "P6\n1280 850\n255\n";
        for (int y = 849; y >= 0; --y) file.write(reinterpret_cast<const char *>(pixels.data() + y * 1280 * 3), 1280 * 3);
        valid = valid && file.good();
    }
    profiler.ClearCapture();
    valid = renderPanel(420) && valid;
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(window); glfwTerminate();
    std::cout << (valid ? "Profiler panel rendering passed\n" : "Profiler panel rendering failed\n");
    return valid ? 0 : 1;
}
