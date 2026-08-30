#include "PlutoGE/ui/EditorCompositor.h"

#include "PlutoGE/platform/Window.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <GLFW/glfw3.h>
#include <vector>

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
                m_textures.clear();
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

            EditorTextureHandle RegisterTexture(const EditorTextureDescriptor &descriptor) override
            {
                if (descriptor.graphicsApi != render::rhi::GraphicsApi::OpenGL || descriptor.nativeHandle == 0)
                    return {};
                for (std::uint32_t index = 0; index < m_textures.size(); ++index)
                {
                    auto &entry = m_textures[index];
                    if (entry.inUse)
                        continue;
                    entry.inUse = true;
                    entry.nativeHandle = descriptor.nativeHandle;
                    return {index, entry.generation};
                }
                m_textures.push_back({descriptor.nativeHandle, 1, true});
                return {static_cast<std::uint32_t>(m_textures.size() - 1), 1};
            }

            void UnregisterTexture(EditorTextureHandle texture) override
            {
                if (!texture.IsValid() || texture.index >= m_textures.size())
                    return;
                auto &entry = m_textures[texture.index];
                if (!entry.inUse || entry.generation != texture.generation)
                    return;
                entry.inUse = false;
                entry.nativeHandle = 0;
                if (++entry.generation == 0)
                    entry.generation = 1;
            }

            std::uint64_t GetImGuiTextureId(EditorTextureHandle texture) const noexcept override
            {
                if (!texture.IsValid() || texture.index >= m_textures.size())
                    return 0;
                const auto &entry = m_textures[texture.index];
                return entry.inUse && entry.generation == texture.generation ? entry.nativeHandle : 0;
            }

            [[nodiscard]] render::rhi::GraphicsApi GetGraphicsApi() const noexcept override
            {
                return render::rhi::GraphicsApi::OpenGL;
            }

        private:
            struct TextureEntry
            {
                std::uint64_t nativeHandle = 0;
                std::uint32_t generation = 1;
                bool inUse = false;
            };

            GLFWwindow *m_window = nullptr;
            std::vector<TextureEntry> m_textures;
        };
    }

    std::unique_ptr<IEditorCompositor> CreateEditorCompositor(render::rhi::GraphicsApi graphicsApi)
    {
        if (graphicsApi == render::rhi::GraphicsApi::OpenGL)
            return std::make_unique<OpenGlEditorCompositor>();
        return {};
    }
}
