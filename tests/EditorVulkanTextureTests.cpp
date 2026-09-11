#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

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
        auto compositor=ui::CreateEditorCompositor(GraphicsApi::Vulkan);
        if(!compositor->Initialize(window,device,*swapchain)) throw std::runtime_error("Compositor initialization failed");
        std::vector<std::byte> pixels(128*128*4,std::byte{255});
        auto color=device.CreateTexture({128,128,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"viewport",true,1,false,1},pixels);
        auto alternate=device.CreateTexture({128,128,Format::R8G8B8A8Unorm,TextureUsage::Sampled,"replacement",true,1,false,1},pixels);
        const auto keepColor=compositor->RegisterTexture({&device,color,128,128});
        const auto keepAlternate=compositor->RegisterTexture({&device,alternate,128,128});
        for(int frame=0;frame<1200;++frame) {
            window.PollEvents(); compositor->BeginFrame(); ImGui::NewFrame();
            ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({128,128});
            ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoDecoration);
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
            ImGui::End(); ImGui::Render(); compositor->RenderDrawData();
            if(!swapchain->Present(color)) throw std::runtime_error("Presentation failed");
        }
        compositor->UnregisterTexture(keepColor); compositor->UnregisterTexture(keepAlternate);
        compositor->Shutdown(); ImGui::DestroyContext();
        device.DestroyTexture(color); device.DestroyTexture(alternate);
        std::cout<<"PASS 1200 Vulkan editor frames, 19200 descriptor replacements, queued draws and stale handles\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
    window.Close();
}
