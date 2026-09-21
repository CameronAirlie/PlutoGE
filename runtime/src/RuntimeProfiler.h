#pragma once

#include "PlutoGE/ui/EditorProfiler.h"
#include "PlutoGE/ui/panels/ProfilerPanel.h"

struct GLFWwindow;
struct ImGuiContext;

namespace PlutoGE
{
    // A separate context keeps profiling UI independent of the game's renderer and input.
    class RuntimeProfiler
    {
    public:
        explicit RuntimeProfiler(render::Renderer &renderer);
        ~RuntimeProfiler();
        bool Initialize(bool visible = true);
        void Draw();
        void Shutdown();
        ui::EditorProfiler profiler;
    private:
        GLFWwindow *m_window = nullptr;
        ImGuiContext *m_context = nullptr;
        ui::ProfilerPanel m_panel;
        bool m_glfwBackend = false;
        bool m_glBackend = false;
    };
}
