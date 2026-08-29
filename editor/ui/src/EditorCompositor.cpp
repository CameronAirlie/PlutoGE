#include "PlutoGE/ui/EditorCompositor.h"

#include "PlutoGE/platform/Window.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <GLFW/glfw3.h>

namespace PlutoGE::ui
{
    namespace
    {
        class OpenGlEditorCompositor final : public IEditorCompositor
        {
        public:
            bool Initialize(platform::Window &window,
                            render::rhi::IRenderDevice &device,
                            render::rhi::ISwapchain &) override
            {
                if (device.GetApi() != render::rhi::GraphicsApi::OpenGL ||
                    window.GetClientApi() != platform::WindowClientApi::OpenGL)
                    return false;
                m_window = static_cast<GLFWwindow *>(window.GetWindow());
                return m_window && ImGui_ImplGlfw_InitForOpenGL(m_window, true) && ImGui_ImplOpenGL3_Init("#version 330 core");
            }

            void Shutdown() override
            {
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                m_window = nullptr;
            }

            void BeginFrame() override
            {
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
            }

            void RenderDrawData() override
            {
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }

            void RenderPlatformWindows() override
            {
                GLFWwindow *previousContext = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(previousContext);
            }

            [[nodiscard]] render::rhi::GraphicsApi GetGraphicsApi() const noexcept override
            {
                return render::rhi::GraphicsApi::OpenGL;
            }

        private:
            GLFWwindow *m_window = nullptr;
        };
    }

    std::unique_ptr<IEditorCompositor> CreateEditorCompositor(render::rhi::GraphicsApi graphicsApi)
    {
        if (graphicsApi == render::rhi::GraphicsApi::OpenGL)
            return std::make_unique<OpenGlEditorCompositor>();
        return {};
    }
}
