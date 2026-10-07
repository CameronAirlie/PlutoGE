#include "PlutoGE/platform/Window.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace
{
    struct BoundedFenceWait
    {
        static inline PFN_vkWaitForFences original = nullptr;
        BoundedFenceWait() { original = vkWaitForFences; vkWaitForFences = Wait; }
        ~BoundedFenceWait() { vkWaitForFences = original; }
        static VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice device, std::uint32_t count,
            const VkFence *fences, VkBool32 all, std::uint64_t timeout)
        {
            // Turn a regression's infinite wait into a test failure.
            return original(device, count, fences, all, (std::min)(timeout, std::uint64_t{5000000000}));
        }
    };

    struct SubmissionProbe
    {
        static inline PFN_vkQueueSubmit original = nullptr;
        static inline unsigned failuresRemaining = 0;
        static inline unsigned calls = 0;
        SubmissionProbe(unsigned failures)
        {
            original = vkQueueSubmit;
            failuresRemaining = failures;
            calls = 0;
            vkQueueSubmit = Submit;
        }
        ~SubmissionProbe() { vkQueueSubmit = original; }
        static VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue queue, std::uint32_t count,
                                                     const VkSubmitInfo *submits, VkFence fence)
        {
            ++calls;
            if (failuresRemaining)
            {
                --failuresRemaining;
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
            return original(queue, count, submits, fence);
        }
    };

    // Inject allocation failures without exhausting the test machine's GPU.
    struct SwapchainCreationProbe
    {
        static inline PFN_vkCreateSwapchainKHR original = nullptr;
        static inline unsigned calls = 0;
        static inline unsigned failuresRemaining = 0;
        static inline VkSwapchainKHR lastOldSwapchain = VK_NULL_HANDLE;
        static inline PFN_vkAcquireNextImageKHR originalAcquire = nullptr;

        SwapchainCreationProbe()
        {
            original = vkCreateSwapchainKHR;
            originalAcquire = vkAcquireNextImageKHR;
            calls = failuresRemaining = 0;
            vkCreateSwapchainKHR = Create;
        }
        ~SwapchainCreationProbe()
        {
            vkCreateSwapchainKHR = original;
            vkAcquireNextImageKHR = originalAcquire;
        }

        static VKAPI_ATTR VkResult VKAPI_CALL AcquireOutOfDate(VkDevice, VkSwapchainKHR, std::uint64_t,
                                                              VkSemaphore, VkFence, std::uint32_t *)
        {
            vkAcquireNextImageKHR = originalAcquire;
            return VK_ERROR_OUT_OF_DATE_KHR;
        }

        static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice device, const VkSwapchainCreateInfoKHR *info,
                                                     const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain)
        {
            ++calls;
            lastOldSwapchain = info->oldSwapchain;
            if (failuresRemaining != 0)
            {
                --failuresRemaining;
                *swapchain = VK_NULL_HANDLE;
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
            return original(device, info, allocator, swapchain);
        }
    };

    std::vector<std::uint32_t> ReadSpirv(const char *name)
    {
        std::ifstream input(std::filesystem::path(PLUTO_RHI_TEST_SHADER_DIR) / name, std::ios::binary | std::ios::ate);
        if (!input) return {};
        const auto size = input.tellg();
        std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(words.data()), size);
        return words;
    }
}

int main(int argc, char **argv)
{
    using namespace PlutoGE;
    platform::Window window;
    if (!window.Create({.title = "PlutoGE Vulkan presentation test",
                        .width = 64,
                        .height = 64,
                        .resizable = false,
                        .visible = false,
                        .clientApi = platform::WindowClientApi::None}))
        return 1;

    try
    {
        const render::rhi::SwapchainDescriptor descriptor{
            .nativeWindow = window.GetWindow(), .width = 64, .height = 64, .vSync = false};
        {
            render::rhi::vulkan::VulkanDevice device(descriptor);
            auto swapchain = device.CreateSwapchain(descriptor);
            if (!swapchain || swapchain->GetWidth() == 0 || swapchain->GetHeight() == 0)
                return 2;
            if (swapchain->IsVSyncEnabled() ||
                !swapchain->SetVSyncEnabled(true) || !swapchain->IsVSyncEnabled() ||
                !swapchain->SetVSyncEnabled(false) || swapchain->IsVSyncEnabled())
                return 8;

            render::BasicRendererShaderPackage shaders;
            shaders.vertex.spirv = ReadSpirv("BasicLit.vertex.spv");
            shaders.instancedVertex.spirv = ReadSpirv("BasicLitInstanced.vertex.spv");
            shaders.fragment.spirv = ReadSpirv("BasicLit.fragment.spv");
            shaders.shadowVertex.spirv = ReadSpirv("DirectionalShadow.vertex.spv");
            shaders.shadowInstancedVertex.spirv = ReadSpirv("DirectionalShadowInstanced.vertex.spv");
            shaders.shadowFragment.spirv = ReadSpirv("DirectionalShadow.fragment.spv");
            const auto loadPostProcess = [&](render::BasicPostProcessEffectType type, const char *module)
            {
                auto &shader = shaders.postProcess[static_cast<std::size_t>(type)];
                shader.vertex.spirv = ReadSpirv((std::string(module) + ".vertex.spv").c_str());
                shader.fragment.spirv = ReadSpirv((std::string(module) + ".fragment.spv").c_str());
            };
            loadPostProcess(render::BasicPostProcessEffectType::ToneMapping, "ToneMapping");
            loadPostProcess(render::BasicPostProcessEffectType::GammaCorrection, "GammaCorrection");
            loadPostProcess(render::BasicPostProcessEffectType::FXAA, "FXAA");
            loadPostProcess(render::BasicPostProcessEffectType::ColorGrading, "ColorGrading");
            loadPostProcess(render::BasicPostProcessEffectType::ChromaticAberration, "ChromaticAberration");
            render::BasicRenderer renderer;
            if (!renderer.Initialize(device, shaders) ||
                !renderer.Resize(swapchain->GetWidth(), swapchain->GetHeight()))
                return 3;
            {
                SwapchainCreationProbe probe;
                if (!swapchain->Resize(swapchain->GetWidth(), swapchain->GetHeight()) || probe.calls != 0)
                    return 9;
                probe.failuresRemaining = 1;
                if (!swapchain->SetVSyncEnabled(true) || probe.calls != 2 ||
                    probe.lastOldSwapchain != VK_NULL_HANDLE)
                    return 10;
                // Both attempts fail; presentation must skip a frame while memory
                // is unavailable, then recover without acquiring a retired chain.
                probe.failuresRemaining = 3;
                if (swapchain->SetVSyncEnabled(false)) return 11;
                renderer.Render(glm::mat4(1.0f), {});
                if (!swapchain->Present(renderer.GetColorTexture()) || probe.failuresRemaining != 0)
                    return 12;
                if (!swapchain->Present(renderer.GetColorTexture())) return 13;
                vkAcquireNextImageKHR = probe.AcquireOutOfDate;
                if (!swapchain->Present(renderer.GetColorTexture()) ||
                    !swapchain->Present(renderer.GetColorTexture())) return 15;
            }
            if (!swapchain->SetVSyncEnabled(false)) return 14;
            {
                SubmissionProbe probe(1);
                renderer.Render(glm::mat4(1.0f), {});
                if (probe.calls != 2 || probe.failuresRemaining != 0) return 16;
            }
            {
                SubmissionProbe probe(1);
                if (!swapchain->Present(renderer.GetColorTexture()) || probe.calls != 2) return 17;
            }
            {
                BoundedFenceWait boundedWait;
                for (unsigned failure = 0; failure < 3; ++failure)
                {
                    SubmissionProbe probe(2);
                    bool threw = false;
                    try { swapchain->Present(renderer.GetColorTexture()); }
                    catch (const std::exception &) { threw = true; }
                    if (!threw || probe.calls != 2) return 20;
                }
                if (!swapchain->Present(renderer.GetColorTexture())) return 21;
            }
            {
                // Recovery must retry the executable buffer, not end it twice.
                SubmissionProbe probe(2);
                auto &context = device.GetImmediateContext();
                context.BeginFrame("Failed submission recovery");
                bool threw = false;
                try { context.Submit(); }
                catch (const std::exception &) { threw = true; }
                if (!threw || probe.calls != 2) return 18;
                context.RecoverInterruptedFrame();
                if (probe.calls != 3) return 19;
                context.BeginFrame("Frame after recovery");
                context.Submit();
            }
            // Exercise many uncapped frame-slot/image-index reuse cycles,
            // including resource recreation while presentation is active.
            const int frameCount = argc > 1 && std::string_view(argv[1]) == "--stress" ? 60000 : 1000;
            for (int frame = 0; frame < frameCount; ++frame)
            {
                if (frame != 0 && frame % 100 == 0)
                {
                    const int size = (frame / 100) % 2 == 0 ? 64 : 80;
                    glfwSetWindowSize(static_cast<GLFWwindow *>(window.GetWindow()), size, size);
                    window.PollEvents();
                    const auto extent = window.GetExtents();
                    if (!swapchain->Resize(extent.width, extent.height) ||
                        !renderer.Resize(swapchain->GetWidth(), swapchain->GetHeight()))
                        return 5;
                }
                window.PollEvents();
                renderer.Render(glm::mat4(1.0f), {});
                if (!swapchain->Present(renderer.GetColorTexture()))
                    return 4;
            }
            if (!swapchain->Resize(64, 64))
                return 5;
            renderer.Render(glm::mat4(1.0f), {});
            if (!swapchain->Present(renderer.GetColorTexture()))
                return 6;
            std::cout << "Presented " << frameCount << " uncapped frames with swapchain recreation\n";
        }
        window.Close();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        window.Close();
        return 7;
    }
    return 0;
}
