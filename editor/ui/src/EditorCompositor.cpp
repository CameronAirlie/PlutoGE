#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/ui/EditorCompositor.h"

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        // ImGui creates a native window for each detached viewport. Wrapping its
        // platform hooks lets those windows deliver OS file drops to the main
        // window, so the content browser receives them wherever it is docked.
        struct ViewportDropForwarding
        {
            platform::Window *target = nullptr;
            void (*create)(ImGuiViewport *) = nullptr;
            void (*destroy)(ImGuiViewport *) = nullptr;
        };

        ViewportDropForwarding &DropForwarding()
        {
            static ViewportDropForwarding forwarding;
            return forwarding;
        }

        void InstallViewportDropForwarding(platform::Window &window)
        {
            auto &forwarding = DropForwarding();
            auto &platformIO = ImGui::GetPlatformIO();
            forwarding.target = &window;
            forwarding.create = platformIO.Platform_CreateWindow;
            forwarding.destroy = platformIO.Platform_DestroyWindow;
            if (forwarding.create)
            {
                platformIO.Platform_CreateWindow = [](ImGuiViewport *viewport)
                {
                    auto &state = DropForwarding();
                    state.create(viewport);
                    if (state.target && viewport->PlatformHandle)
                        state.target->ForwardDropsFrom(static_cast<GLFWwindow *>(viewport->PlatformHandle));
                };
            }
            if (forwarding.destroy)
            {
                platformIO.Platform_DestroyWindow = [](ImGuiViewport *viewport)
                {
                    auto &state = DropForwarding();
                    if (state.target && viewport->PlatformHandle)
                        state.target->StopForwardingDropsFrom(static_cast<GLFWwindow *>(viewport->PlatformHandle));
                    state.destroy(viewport);
                };
            }
        }

        // Call before the GLFW backend shuts down, which destroys viewport windows.
        void RemoveViewportDropForwarding()
        {
            auto &forwarding = DropForwarding();
            if (ImGui::GetCurrentContext())
            {
                auto &platformIO = ImGui::GetPlatformIO();
                if (forwarding.create) platformIO.Platform_CreateWindow = forwarding.create;
                if (forwarding.destroy) platformIO.Platform_DestroyWindow = forwarding.destroy;
            }
            forwarding = {};
        }

        // Detached windows on Vulkan use FIFO with enough images that a
        // non-blocking acquire usually succeeds. They refresh at display rate
        // and never hold back the editor loop, whatever its vsync setting.
        constexpr std::uint32_t kDetachedViewportImageCount = 3;

        class VulkanEditorCompositor;

        // ImGui renderer hooks are plain function pointers, so the compositor
        // that joins detached viewports to the host present is found here.
        struct JoinedViewportHooks
        {
            VulkanEditorCompositor *owner = nullptr;
            void (*create)(ImGuiViewport *) = nullptr;
            void (*render)(ImGuiViewport *, void *) = nullptr;
            void (*swap)(ImGuiViewport *, void *) = nullptr;
        };

        JoinedViewportHooks &JoinedHooks()
        {
            static JoinedViewportHooks hooks;
            return hooks;
        }

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
                if (!m_window || !ImGui_ImplGlfw_InitForOpenGL(m_window, true) || !ImGui_ImplOpenGL3_Init("#version 330 core"))
                    return false;
                InstallViewportDropForwarding(window);
                return true;
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
                RemoveViewportDropForwarding();
                ImGui_ImplGlfw_Shutdown();
                m_window = nullptr;
                m_device = nullptr;
            }

            void BeginFrame() override
            {
                { core::CpuScope scope("ImGui OpenGL backend new frame", core::CpuCategory::UI); ImGui_ImplOpenGL3_NewFrame(); }
                { core::CpuScope scope("ImGui GLFW new frame", core::CpuCategory::UI); ImGui_ImplGlfw_NewFrame(); }
            }

            void RenderDrawData(ImDrawData *drawData) override
            {
                if (drawData) ImGui_ImplOpenGL3_RenderDrawData(drawData);
            }

            void RenderPlatformWindows(bool updateWindows) override
            {
                GLFWwindow *previousContext = glfwGetCurrentContext();
                if (updateWindows) ImGui::UpdatePlatformWindows();
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
                    entry = {nativeHandle, entry.generation, true, descriptor.device && descriptor.device->GetApi() == render::rhi::GraphicsApi::Vulkan};
                    const EditorTextureHandle handle{index, entry.generation};
                    UpdateTexture(handle, descriptor);
                    return handle;
                }
                m_textures.push_back({nativeHandle, 1, true, descriptor.device && descriptor.device->GetApi() == render::rhi::GraphicsApi::Vulkan});
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

        class VulkanEditorCompositor final : public IEditorCompositor
        {
        public:
            bool Initialize(platform::Window &window, render::rhi::IRenderDevice &device,
                            render::rhi::ISwapchain &swapchain) override
            {
                if (device.GetApi() != render::rhi::GraphicsApi::Vulkan ||
                    window.GetClientApi() != platform::WindowClientApi::None)
                {
                    std::cerr << "Vulkan editor compositor requires a Vulkan device and no-client-API window.\n"
                              << std::flush;
                    return false;
                }
                auto &vulkanDevice = static_cast<render::rhi::vulkan::VulkanDevice &>(device);
                const auto context = vulkanDevice.GetEditorContext(swapchain);
                m_window = static_cast<GLFWwindow *>(window.GetWindow());
                if (!context || !m_window || context->imageCount < 2)
                {
                    std::cerr << "Vulkan editor compositor could not acquire a valid device/swapchain context.\n"
                              << std::flush;
                    return false;
                }

                m_device = &vulkanDevice;
                m_swapchain = &swapchain;
                m_context = *context;
                if (!ImGui_ImplGlfw_InitForVulkan(m_window, true))
                {
                    std::cerr << "ImGui GLFW Vulkan platform backend initialization failed.\n"
                              << std::flush;
                    return false;
                }

                ImGui_ImplVulkan_InitInfo init{};
                init.ApiVersion = VK_API_VERSION_1_3;
                init.Instance = context->instance;
                init.PhysicalDevice = context->physicalDevice;
                init.Device = context->device;
                init.QueueFamily = context->queueFamily;
                init.Queue = context->queue;
                init.DescriptorPoolSize = 1024;
                // MinImageCount only sizes detached viewport swapchains. ImageCount
                // sizes ImGui's per-viewport buffer rings and must be at least that.
                init.MinImageCount = kDetachedViewportImageCount;
                init.ImageCount = std::max(context->imageCount, kDetachedViewportImageCount);
                init.UseDynamicRendering = true;
                init.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
                init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_context.swapchainFormat;
                init.PipelineInfoForViewports.PipelineRenderingCreateInfo = init.PipelineInfoMain.PipelineRenderingCreateInfo;
                if (!ImGui_ImplVulkan_Init(&init))
                {
                    std::cerr << "ImGui Vulkan renderer backend initialization failed.\n"
                              << std::flush;
                    ImGui_ImplGlfw_Shutdown();
                    return false;
                }
                swapchain.SetOverlayRecorder([this](void *commandContext)
                                             {
                    if (m_drawData)
                        ImGui_ImplVulkan_RenderDrawData(m_drawData, static_cast<VkCommandBuffer>(commandContext));
                    m_drawData = nullptr; });
                swapchain.SetOverlayPreparation([this](void *commandContext)
                                                {
                    for (const auto &entry : m_textures)
                        if (entry.inUse)
                            m_device->PrepareTextureForEditorSampling(commandContext, entry.texture); });
                InstallJoinedViewports();
                InstallViewportDropForwarding(window);
                return true;
            }

            void Shutdown() override
            {
                if (m_context.device)
                    vkDeviceWaitIdle(m_context.device);
                if (m_swapchain)
                {
                    m_swapchain->SetOverlayPreparation({});
                    m_swapchain->SetOverlayRecorder({});
                    m_device->SetJoinedPresenter(*m_swapchain, {});
                }
                RemoveJoinedViewports();
                for (const auto &entry : m_textures)
                    if (entry.inUse && entry.descriptorSet)
                        ImGui_ImplVulkan_RemoveTexture(entry.descriptorSet);
                m_textures.clear();
                for (auto descriptor : m_pendingDescriptors)
                    ImGui_ImplVulkan_RemoveTexture(descriptor);
                m_pendingDescriptors.clear();
                for (auto &batch : m_retiredDescriptors) {
                    for (auto descriptor : batch.descriptors) ImGui_ImplVulkan_RemoveTexture(descriptor);
                    vkDestroyFence(m_context.device, batch.fence, nullptr);
                }
                m_retiredDescriptors.clear();
                ImGui_ImplVulkan_Shutdown();
                RemoveViewportDropForwarding();
                ImGui_ImplGlfw_Shutdown();
                m_drawData = nullptr;
                m_swapchain = nullptr;
                m_device = nullptr;
                m_window = nullptr;
                m_context = {};
            }

            void BeginFrame() override
            {
                CollectRetiredDescriptors();
                { core::CpuScope scope("ImGui Vulkan backend new frame", core::CpuCategory::UI); ImGui_ImplVulkan_NewFrame(); }
                { core::CpuScope scope("ImGui GLFW new frame", core::CpuCategory::UI); ImGui_ImplGlfw_NewFrame(); }
            }

            void RenderDrawData(ImDrawData *drawData) override { m_drawData = drawData; }

            void RenderPlatformWindows(bool updateWindows) override
            {
                if (updateWindows) ImGui::UpdatePlatformWindows();
                // Join only while the host swapchain is presenting. When it skips
                // frames (minimised, out of date) ImGui presents each window itself.
                m_joinViewports = m_hostPresented;
                m_hostPresented = false;
                m_joinedViewports.clear();
                ImGui::RenderPlatformWindowsDefault();
            }

            EditorTextureHandle RegisterTexture(const EditorTextureDescriptor &descriptor) override
            {
                if (descriptor.device != m_device || !descriptor.texture)
                    return {};
                const VkImageView imageView = m_device->GetTextureImageView(descriptor.texture);
                if (!imageView)
                    return {};
                const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
                    imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (!descriptorSet)
                    return {};
                for (std::uint32_t index = 0; index < m_textures.size(); ++index)
                {
                    auto &entry = m_textures[index];
                    if (entry.inUse)
                        continue;
                    entry = {descriptorSet, entry.generation, true, descriptor.texture};
                    return {index, entry.generation};
                }
                m_textures.push_back({descriptorSet, 1, true, descriptor.texture});
                return {static_cast<std::uint32_t>(m_textures.size() - 1), 1};
            }

            void UpdateTexture(EditorTextureHandle texture, const EditorTextureDescriptor &descriptor) override
            {
                if (!texture.IsValid() || texture.index >= m_textures.size())
                    return;
                auto &entry = m_textures[texture.index];
                if (!entry.inUse || entry.generation != texture.generation || entry.texture == descriptor.texture)
                    return;
                if (descriptor.device != m_device || !descriptor.texture)
                    return;
                const VkImageView imageView = m_device->GetTextureImageView(descriptor.texture);
                if (!imageView)
                    return;
                const VkDescriptorSet replacement = ImGui_ImplVulkan_AddTexture(
                    imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (!replacement)
                    return;
                m_pendingDescriptors.push_back(entry.descriptorSet);
                entry.descriptorSet = replacement;
                entry.texture = descriptor.texture;
            }

            void UnregisterTexture(EditorTextureHandle texture) override
            {
                if (!texture.IsValid() || texture.index >= m_textures.size())
                    return;
                auto &entry = m_textures[texture.index];
                if (!entry.inUse || entry.generation != texture.generation)
                    return;
                m_pendingDescriptors.push_back(entry.descriptorSet);
                entry.inUse = false;
                entry.descriptorSet = VK_NULL_HANDLE;
                entry.texture = {};
                if (++entry.generation == 0)
                    entry.generation = 1;
            }

            std::uint64_t GetImGuiTextureId(EditorTextureHandle texture) const noexcept override
            {
                if (!texture.IsValid() || texture.index >= m_textures.size())
                    return 0;
                const auto &entry = m_textures[texture.index];
                return entry.inUse && entry.generation == texture.generation
                           ? reinterpret_cast<std::uint64_t>(entry.descriptorSet)
                           : 0;
            }

            render::rhi::GraphicsApi GetGraphicsApi() const noexcept override
            {
                return render::rhi::GraphicsApi::Vulkan;
            }

        private:
            void InstallJoinedViewports()
            {
                auto &hooks = JoinedHooks();
                auto &platformIO = ImGui::GetPlatformIO();
                if (!platformIO.Renderer_CreateWindow || !platformIO.Renderer_RenderWindow ||
                    !platformIO.Renderer_SwapBuffers)
                    return;
                hooks = {this, platformIO.Renderer_CreateWindow, platformIO.Renderer_RenderWindow,
                         platformIO.Renderer_SwapBuffers};
                platformIO.Renderer_CreateWindow = [](ImGuiViewport *viewport)
                {
                    auto &state = JoinedHooks();
                    state.create(viewport);
                    if (state.owner) state.owner->UseFifoPresentation(viewport);
                };
                platformIO.Renderer_RenderWindow = [](ImGuiViewport *viewport, void *argument)
                {
                    auto &state = JoinedHooks();
                    if (!state.owner || !state.owner->JoinViewport(viewport))
                        state.render(viewport, argument);
                };
                platformIO.Renderer_SwapBuffers = [](ImGuiViewport *viewport, void *argument)
                {
                    auto &state = JoinedHooks();
                    if (!state.owner || !state.owner->IsJoined(viewport))
                        state.swap(viewport, argument);
                };
                m_device->SetJoinedPresenter(*m_swapchain, {
                    [this](VkCommandBuffer commandBuffer, std::vector<render::rhi::vulkan::VulkanJoinedPresentation> &joined)
                    { RecordJoinedViewports(commandBuffer, joined); },
                    [this](const std::vector<render::rhi::vulkan::VulkanJoinedPresentation> &joined)
                    { OnJoinedViewportsPresented(joined); }});
            }

            // Call before the Vulkan backend shuts down.
            void RemoveJoinedViewports()
            {
                auto &hooks = JoinedHooks();
                if (hooks.owner == this && ImGui::GetCurrentContext())
                {
                    auto &platformIO = ImGui::GetPlatformIO();
                    platformIO.Renderer_CreateWindow = hooks.create;
                    platformIO.Renderer_RenderWindow = hooks.render;
                    platformIO.Renderer_SwapBuffers = hooks.swap;
                }
                if (hooks.owner == this)
                    hooks = {};
                m_joinedViewports.clear();
                m_presentedViewports.clear();
                m_rebuildViewports.clear();
            }

            void ResizeViewportSwapchain(ImGuiViewport *viewport, ImGui_ImplVulkanH_Window &window)
            {
                ImGui_ImplVulkanH_CreateOrResizeWindow(m_context.instance, m_context.physicalDevice, m_context.device,
                                                       &window, m_context.queueFamily, nullptr,
                                                       static_cast<int>(viewport->Size.x),
                                                       static_cast<int>(viewport->Size.y),
                                                       kDetachedViewportImageCount, 0);
            }

            // The backend prefers MAILBOX/IMMEDIATE, which still blocks once per
            // vblank per window on drivers without MAILBOX. FIFO plus a
            // non-blocking acquire never waits.
            void UseFifoPresentation(ImGuiViewport *viewport)
            {
                auto *window = ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport);
                if (!window || !window->Swapchain || window->PresentMode == VK_PRESENT_MODE_FIFO_KHR)
                    return;
                window->PresentMode = VK_PRESENT_MODE_FIFO_KHR;
                ResizeViewportSwapchain(viewport, *window);
            }

            // Renderer_RenderWindow: defers the viewport to the host present.
            // Returns false to leave it to the backend's own render and swap.
            bool JoinViewport(ImGuiViewport *viewport)
            {
                auto *window = ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport);
                // The host pipeline renders joined windows, so formats must match.
                if (!m_joinViewports || !window || !window->Swapchain ||
                    window->SurfaceFormat.format != m_context.swapchainFormat)
                    return false;
                if (m_rebuildViewports.erase(viewport->ID))
                    ResizeViewportSwapchain(viewport, *window);
                m_joinedViewports.push_back(viewport->ID);
                return true;
            }

            [[nodiscard]] bool IsJoined(const ImGuiViewport *viewport) const
            {
                return std::ranges::find(m_joinedViewports, viewport->ID) != m_joinedViewports.end();
            }

            void RecordJoinedViewports(VkCommandBuffer commandBuffer,
                                       std::vector<render::rhi::vulkan::VulkanJoinedPresentation> &joined)
            {
                m_hostPresented = true;
                m_presentedViewports.clear();
                if (m_joinedViewports.empty() || !ImGui::GetCurrentContext())
                    return;
                for (const ImGuiID id : m_joinedViewports)
                {
                    ImGuiViewport *viewport = ImGui::FindViewportByID(id);
                    auto *window = viewport ? ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport) : nullptr;
                    // NewFrame clears DrawData, so a stale entry is skipped here.
                    if (!window || !window->Swapchain || !viewport->DrawData || !viewport->DrawData->Valid)
                        continue;
                    auto &semaphores = window->FrameSemaphores[window->SemaphoreIndex];
                    std::uint32_t image = 0;
                    const VkResult acquired = vkAcquireNextImageKHR(m_context.device, window->Swapchain, 0,
                                                                    semaphores.ImageAcquiredSemaphore,
                                                                    VK_NULL_HANDLE, &image);
                    // No free image means the window is still showing queued
                    // frames. Skip it this frame rather than wait.
                    if (acquired == VK_NOT_READY || acquired == VK_TIMEOUT)
                        continue;
                    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
                    {
                        m_rebuildViewports.insert(id);
                        continue;
                    }
                    if (acquired == VK_SUBOPTIMAL_KHR)
                        m_rebuildViewports.insert(id);
                    window->FrameIndex = image;
                    const auto &frame = window->Frames[image];

                    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = frame.Backbuffer;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                                         1, &barrier);
                    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                    color.imageView = frame.BackbufferView;
                    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
                    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
                    rendering.renderArea.extent = {static_cast<std::uint32_t>(window->Width),
                                                   static_cast<std::uint32_t>(window->Height)};
                    rendering.layerCount = 1;
                    rendering.colorAttachmentCount = 1;
                    rendering.pColorAttachments = &color;
                    vkCmdBeginRendering(commandBuffer, &rendering);
                    ImGui_ImplVulkan_RenderDrawData(viewport->DrawData, commandBuffer);
                    vkCmdEndRendering(commandBuffer);

                    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier.dstAccessMask = 0;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                         &barrier);
                    joined.push_back({window->Swapchain, image, semaphores.ImageAcquiredSemaphore,
                                      semaphores.RenderCompleteSemaphore});
                    m_presentedViewports.push_back(id);
                }
                m_joinedViewports.clear();
            }

            void OnJoinedViewportsPresented(const std::vector<render::rhi::vulkan::VulkanJoinedPresentation> &joined)
            {
                for (std::size_t index = 0; index < joined.size() && index < m_presentedViewports.size(); ++index)
                {
                    const ImGuiID id = m_presentedViewports[index];
                    const VkResult result = joined[index].result;
                    if (result != VK_SUCCESS)
                        m_rebuildViewports.insert(id);
                    // A rebuild recreates the semaphore ring, so only advance it
                    // after a present that consumed this set.
                    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
                        continue;
                    ImGuiViewport *viewport = ImGui::FindViewportByID(id);
                    if (auto *window = viewport ? ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport) : nullptr)
                        window->SemaphoreIndex = (window->SemaphoreIndex + 1) % window->SemaphoreCount;
                }
                m_presentedViewports.clear();
            }

            // Viewports deferred to the host present this frame, and those it
            // recorded, in the order of its joined entries.
            std::vector<ImGuiID> m_joinedViewports;
            std::vector<ImGuiID> m_presentedViewports;
            std::unordered_set<ImGuiID> m_rebuildViewports;
            bool m_joinViewports = false;
            bool m_hostPresented = true;

            struct RetiredDescriptors
            {
                VkFence fence = VK_NULL_HANDLE;
                std::vector<VkDescriptorSet> descriptors;
            };

            void CollectRetiredDescriptors()
            {
                // Called before NewFrame: the preceding ImGui draw data and
                // platform windows have now been submitted. Never recycle a
                // descriptor while either CPU draw data or GPU work can use it.
                for (auto it=m_retiredDescriptors.begin(); it!=m_retiredDescriptors.end();) {
                    const auto status=vkGetFenceStatus(m_context.device,it->fence);
                    if(status==VK_NOT_READY) { ++it; continue; }
                    if(status!=VK_SUCCESS) throw std::runtime_error("Vulkan editor descriptor retirement fence failed");
                    for(auto descriptor:it->descriptors) ImGui_ImplVulkan_RemoveTexture(descriptor);
                    vkDestroyFence(m_context.device,it->fence,nullptr);
                    it=m_retiredDescriptors.erase(it);
                }
                if(m_pendingDescriptors.empty()) return;
                VkFence fence=VK_NULL_HANDLE;
                VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                if(vkCreateFence(m_context.device,&info,nullptr,&fence)!=VK_SUCCESS)
                    throw std::runtime_error("Could not create editor descriptor retirement fence");
                // An empty submission fences all earlier work on the shared
                // graphics queue without a device-wide idle or a CPU wait.
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                if(vkQueueSubmit(m_context.queue,1,&submit,fence)!=VK_SUCCESS) {
                    vkDestroyFence(m_context.device,fence,nullptr);
                    throw std::runtime_error("Could not submit editor descriptor retirement fence");
                }
                m_retiredDescriptors.push_back({fence,std::move(m_pendingDescriptors)});
                m_pendingDescriptors.clear();
            }

            std::vector<VkDescriptorSet> m_pendingDescriptors;
            std::vector<RetiredDescriptors> m_retiredDescriptors;
            struct TextureEntry
            {
                VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
                std::uint32_t generation = 1;
                bool inUse = false;
                render::rhi::TextureHandle texture;
            };

            GLFWwindow *m_window = nullptr;
            render::rhi::vulkan::VulkanDevice *m_device = nullptr;
            render::rhi::ISwapchain *m_swapchain = nullptr;
            render::rhi::vulkan::VulkanEditorContext m_context;
            ImDrawData *m_drawData = nullptr;
            std::vector<TextureEntry> m_textures;
        };
    }

    std::unique_ptr<IEditorCompositor> CreateEditorCompositor(render::rhi::GraphicsApi graphicsApi)
    {
        if (graphicsApi == render::rhi::GraphicsApi::OpenGL)
            return std::make_unique<OpenGlEditorCompositor>();
        if (graphicsApi == render::rhi::GraphicsApi::Vulkan)
            return std::make_unique<VulkanEditorCompositor>();
        return {};
    }
}
