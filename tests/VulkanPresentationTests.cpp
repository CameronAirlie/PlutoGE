#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
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

int main()
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
            for (int frame = 0; frame < 4; ++frame)
            {
                renderer.Render(glm::mat4(1.0f), {});
                if (!swapchain->Present(renderer.GetColorTexture()))
                    return 4;
            }
            if (!swapchain->Resize(64, 64))
                return 5;
            renderer.Render(glm::mat4(1.0f), {});
            if (!swapchain->Present(renderer.GetColorTexture()))
                return 6;
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
