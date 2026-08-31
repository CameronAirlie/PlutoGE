#include "PlutoGE/ui/EditorCompositor.h"

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <GLFW/glfw3.h>
#include <limits>
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
                m_device = &device;
                m_window = static_cast<GLFWwindow *>(window.GetWindow());
                return m_window && ImGui_ImplGlfw_InitForOpenGL(m_window, true) && ImGui_ImplOpenGL3_Init("#version 330 core");
            }

            void Shutdown() override
            {
                for (const auto &entry : m_textures)
                    if (entry.inUse && entry.ownedImport && entry.nativeHandle)
                    {
                        const GLuint nativeTexture = static_cast<GLuint>(entry.nativeHandle);
                        glDeleteTextures(1, &nativeTexture);
                    }
                m_textures.clear();
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                m_window = nullptr;
                m_device = nullptr;
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
                std::uint64_t nativeHandle = descriptor.nativeOpenGlTexture;
                if (descriptor.device || descriptor.texture)
                {
                    if (!descriptor.device || !descriptor.texture)
                        return {};
                    if (descriptor.device->GetApi() == render::rhi::GraphicsApi::OpenGL)
                        nativeHandle = static_cast<render::rhi::opengl::OpenGLDevice &>(*descriptor.device)
                                           .GetTextureNativeHandle(descriptor.texture);
                    else
                    {
                        GLuint importedTexture = 0;
                        glGenTextures(1, &importedTexture);
                        nativeHandle = importedTexture;
                    }
                }
                if (nativeHandle == 0)
                    return {};
                for (std::uint32_t index = 0; index < m_textures.size(); ++index)
                {
                    auto &entry = m_textures[index];
                    if (entry.inUse)
                        continue;
                    entry.inUse = true;
                    entry = {nativeHandle, entry.generation, true, descriptor.device &&
                        descriptor.device->GetApi() == render::rhi::GraphicsApi::Vulkan};
                    const EditorTextureHandle handle{index, entry.generation};
                    UpdateTexture(handle, descriptor);
                    return handle;
                }
                m_textures.push_back({nativeHandle, 1, true, descriptor.device &&
                    descriptor.device->GetApi() == render::rhi::GraphicsApi::Vulkan});
                const EditorTextureHandle handle{static_cast<std::uint32_t>(m_textures.size() - 1), 1};
                UpdateTexture(handle, descriptor);
                return handle;
            }

            void UpdateTexture(EditorTextureHandle texture, const EditorTextureDescriptor &descriptor) override
            {
                if (!texture.IsValid() || texture.index >= m_textures.size() || !descriptor.device ||
                    descriptor.device->GetApi() != render::rhi::GraphicsApi::Vulkan || !descriptor.texture ||
                    descriptor.width == 0 || descriptor.height == 0)
                    return;
                auto &entry = m_textures[texture.index];
                if (!entry.inUse || entry.generation != texture.generation || !entry.ownedImport)
                    return;
                const auto pixels = static_cast<render::rhi::vulkan::VulkanDevice &>(*descriptor.device)
                                        .ReadTextureRgba8Buffered(descriptor.texture);
                if (!pixels)
                    return;

                // Buffered readback intentionally trails rendering by a frame.
                // During a viewport/fullscreen resize that completed frame can
                // still have the old extent. Never ask the GL driver to consume
                // more bytes than the readback actually owns.
                constexpr std::size_t bytesPerPixel = 4;
                if (descriptor.width > std::numeric_limits<std::size_t>::max() / descriptor.height ||
                    static_cast<std::size_t>(descriptor.width) * descriptor.height >
                        std::numeric_limits<std::size_t>::max() / bytesPerPixel)
                    return;
                const std::size_t expectedSize = static_cast<std::size_t>(descriptor.width) *
                                                 descriptor.height * bytesPerPixel;
                if (pixels->size() != expectedSize)
                    return;

                GLFWwindow *previousContext = glfwGetCurrentContext();
                if (previousContext != m_window)
                    glfwMakeContextCurrent(m_window);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(entry.nativeHandle));
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                if (entry.width != descriptor.width || entry.height != descriptor.height)
                {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, static_cast<GLsizei>(descriptor.width),
                                 static_cast<GLsizei>(descriptor.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());
                    entry.width = descriptor.width;
                    entry.height = descriptor.height;
                }
                else
                {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(descriptor.width),
                                    static_cast<GLsizei>(descriptor.height), GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());
                }
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                if (previousContext != m_window)
                    glfwMakeContextCurrent(previousContext);
            }

            void UnregisterTexture(EditorTextureHandle texture) override
            {
                if (!texture.IsValid() || texture.index >= m_textures.size())
                    return;
                auto &entry = m_textures[texture.index];
                if (!entry.inUse || entry.generation != texture.generation)
                    return;
                if (entry.ownedImport && entry.nativeHandle)
                {
                    const GLuint nativeTexture = static_cast<GLuint>(entry.nativeHandle);
                    glDeleteTextures(1, &nativeTexture);
                }
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
                bool ownedImport = false;
                std::uint32_t width = 0;
                std::uint32_t height = 0;
            };

            GLFWwindow *m_window = nullptr;
            render::rhi::IRenderDevice *m_device = nullptr;
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
