#ifdef _WIN32
#include <windows.h>
#endif

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

int main()
{
    using namespace PlutoGE;
    using namespace render::rhi;
    platform::Window window;
    if(!window.Create({.title="Vulkan editor texture stress",.width=128,.height=128,.visible=false,.clientApi=platform::WindowClientApi::None})) return 1;
    try {
        SwapchainDescriptor description{.nativeWindow=window.GetWindow(),.width=128,.height=128,.vSync=false};
        vulkan::VulkanDevice device(description);
        auto swapchain=device.CreateSwapchain(description);
        ImGui::CreateContext(); ImGui::GetIO().IniFilename=nullptr;
        // Synthetic input is sequenced by test frames, including native capture
        // events. Consume each frame's complete event batch deterministically.
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        auto compositor=ui::CreateEditorCompositor(GraphicsApi::Vulkan);
        if(!compositor->Initialize(window,device,*swapchain)) throw std::runtime_error("Compositor initialization failed");
        auto *native = static_cast<GLFWwindow *>(window.GetWindow());
        window.SetResizeCallback([&](int width, int height) {
            if (width > 0 && height > 0 && !swapchain->Resize(width, height))
                throw std::runtime_error("Editor swapchain resize failed");
        });
        const auto mouseCallback = glfwSetMouseButtonCallback(native, nullptr);
        glfwSetMouseButtonCallback(native, mouseCallback);
        if (!mouseCallback) throw std::runtime_error("ImGui mouse callback missing");
        int buttonClicks = 0;
        std::vector<std::byte> pixels(128*128*4,std::byte{255});
        auto color=device.CreateTexture({128,128,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"viewport",true,1,false,1},pixels);
        auto alternate=device.CreateTexture({128,128,Format::R8G8B8A8Unorm,TextureUsage::Sampled,"replacement",true,1,false,1},pixels);
        const auto keepColor=compositor->RegisterTexture({&device,color,128,128});
        const auto keepAlternate=compositor->RegisterTexture({&device,alternate,128,128});
        for(int frame=0;frame<1200;++frame) {
            const int inputFrame = frame % 400;
#ifdef _WIN32
            // Losing this native association leaves polling-based hover alive
            // but makes GLFW discard mouse, keyboard and resize messages.
            if (inputFrame == 10)
                RemovePropW(glfwGetWin32Window(native), L"GLFW");
#endif
            if (frame == 400 || frame == 800) {
                // Reproduce a missed callback while the native window grows
                // beyond its initial presentation extent, then shrinks again.
                glfwSetFramebufferSizeCallback(native, nullptr);
                glfwSetWindowSize(native, frame == 400 ? 256 : 128, frame == 400 ? 192 : 128);
            }
            window.PollEvents(); compositor->BeginFrame();
#ifdef _WIN32
            if (GetPropW(glfwGetWin32Window(native), L"GLFW") != native)
                throw std::runtime_error("Native GLFW event routing was not recovered");
#endif
            if (inputFrame >= 10 && inputFrame <= 12) {
                if (inputFrame > 10) {
#ifdef _WIN32
                    SendMessageW(glfwGetWin32Window(native), inputFrame == 11 ? WM_LBUTTONDOWN : WM_LBUTTONUP,
                                 inputFrame == 11 ? MK_LBUTTON : 0, MAKELPARAM(24, 24));
#else
                    mouseCallback(native, GLFW_MOUSE_BUTTON_LEFT, inputFrame == 11 ? GLFW_PRESS : GLFW_RELEASE, 0);
#endif
                    if (window.GetInputState().mouseState.buttons[0] != (inputFrame == 11))
                        throw std::runtime_error("ImGui did not chain the engine mouse callback");
                }
                // Keep the hidden test deterministic without moving the user's
                // desktop cursor when Windows changes capture on button down.
                ImGui_ImplGlfw_CursorPosCallback(native, 24, 24);
            }
#ifdef _WIN32
            if (inputFrame == 13 || inputFrame == 14) {
                SendMessageW(glfwGetWin32Window(native), inputFrame == 13 ? WM_KEYDOWN : WM_KEYUP,
                             'A', inputFrame == 13 ? 0x001e0001 : 0xc01e0001);
                if (window.GetInputState().keys[GLFW_KEY_A] != (inputFrame == 13))
                    throw std::runtime_error("Native keyboard routing did not recover");
            }
#endif
            ImGui::NewFrame();
            const auto extent = window.GetExtents();
            if (swapchain->GetWidth() != extent.width || swapchain->GetHeight() != extent.height)
                throw std::runtime_error("Editor presentation retained an outdated framebuffer extent");
            ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoDecoration);
            if (ImGui::Button("Input check", {96, 24})) ++buttonClicks;
            ImGui::Text("Font atlas frame %d",frame);
            std::unordered_set<std::uint64_t> recorded;
            // Retire descriptors AFTER they have been put into CPU draw lists.
            // They must survive until these lists are submitted and completed.
            for(int replacement=0;replacement<8;++replacement) {
                auto handle=compositor->RegisterTexture({&device,color,128,128});
                auto id=compositor->GetImGuiTextureId(handle);
                if(!id || !recorded.insert(id).second) throw std::runtime_error("Descriptor recycled before pending draw submission");
                ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(id),{8,40},{120,120});
                compositor->UpdateTexture(handle,{&device,alternate,128,128});
                id=compositor->GetImGuiTextureId(handle);
                if(!id || !recorded.insert(id).second) throw std::runtime_error("Replacement reused a queued descriptor");
                ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(id),{40,40},{80,80});
                compositor->UnregisterTexture(handle);
                if(compositor->GetImGuiTextureId(handle)!=0) throw std::runtime_error("Stale registration still exposed");
            }
            ImGui::End(); ImGui::Render(); compositor->RenderDrawData(ImGui::GetDrawData());
            if(!swapchain->Present(color)) throw std::runtime_error("Presentation failed");
        }
        if (buttonClicks != 3) throw std::runtime_error("ImGui button did not respond to GLFW press/release after routing recovery");
        compositor->UnregisterTexture(keepColor); compositor->UnregisterTexture(keepAlternate);
        compositor->Shutdown(); ImGui::DestroyContext();
        device.DestroyTexture(color); device.DestroyTexture(alternate);
        std::cout<<"PASS 1200 Vulkan editor frames, 19200 descriptor replacements, resize recovery, repeated native routing recovery, ImGui clicks, keyboard and callback chaining\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
    window.Close();
}
