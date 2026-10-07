#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/ui/EditorSceneRenderService.h"
#include <glad/glad.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    const auto api = argc > 1 && std::string_view(argv[1]) == "--opengl"
        ? render::rhi::GraphicsApi::OpenGL : render::rhi::GraphicsApi::Vulkan;
    auto &engine = core::Engine::GetInstance();
    core::EngineConfig config;
    config.graphicsApi = api;
    config.windowConfig.visible = false;
    config.windowConfig.width = config.windowConfig.height = 64;
    if (!engine.Initialize(config)) throw std::runtime_error("Engine initialization failed");
    struct EngineScope { core::Engine &engine; ~EngineScope() { engine.Shutdown(); } } scope{engine};
    const auto directory = std::filesystem::temp_directory_path() / "PlutoGE-camera-ui-visibility";
    std::filesystem::create_directories(directory / "Assets/UI");
    std::ofstream(directory / "Assets/UI/test.rml") << R"(<rml><head><style>
        body { width: 100%; height: 100%; margin: 0; background-color: #ff0000; }
        </style></head><body/></rml>)";
    engine.GetAssetManager().SetProjectContext(directory.string());
    scene::Scene scene;
    auto *owner = scene.AddEntity(std::make_unique<scene::Entity>());
    owner->CreateComponent<scene::RmlWidgetComponent>()->SetSource("project://UI/test.rml");
    ui::EditorSceneRenderService service;
    if (!service.Initialize(api, engine.GetRenderDevice())) throw std::runtime_error("Viewport initialization failed");
    render::CameraData camera;
    camera.view = camera.projection = glm::mat4(1);
    auto *device = service.GetRenderDevice();
    for (bool visible : {false, true, false, true, false})
    {
        camera.renderRuntimeUI = visible;
        if (!service.Render(64, 64, camera, {}, {}, {}, &scene, render::PostProcessDebugView::None))
            throw std::runtime_error(service.GetLastRenderError());
        std::vector<std::byte> pixels;
        if (auto *vk = dynamic_cast<render::rhi::vulkan::VulkanDevice *>(device))
            pixels = vk->ReadTextureRgba8(service.GetViewportTexture());
        else
        {
            auto *gl = dynamic_cast<render::rhi::opengl::OpenGLDevice *>(device);
            pixels.resize(64 * 64 * 4);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(gl->GetTextureNativeHandle(service.GetViewportTexture())));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        const auto index = (32 * 64 + 32) * 4;
        const bool red = std::to_integer<unsigned>(pixels[index]) > 200 &&
                         std::to_integer<unsigned>(pixels[index + 1]) < 30 &&
                         std::to_integer<unsigned>(pixels[index + 2]) < 30;
        if (red != visible) throw std::runtime_error("Runtime UI leaked or disappeared when switching cameras");
    }
    std::cout << "PASS: runtime UI follows camera visibility across repeated editor/game camera switches\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
