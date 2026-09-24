#include "PlutoGE/core/SceneLoading.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/LoadingWork.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void Check(bool condition, const char *message)
    { if (!condition) throw std::runtime_error(message); }
}

int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    using namespace std::chrono_literals;
    const auto owner = std::this_thread::get_id();
    unsigned frames = 0;
    {
        platform::LoadingWork work([&]
        {
            Check(std::this_thread::get_id() == owner, "Presentation escaped the owner thread");
            ++frames;
            platform::LoadingWork::Checkpoint(true); // Must not reenter presentation.
        }, 1ms);
        const auto worker = platform::LoadingWork::Prepare([]
        {
            Check(!platform::LoadingWork::IsActive(), "Worker inherited main-thread loading context");
            std::this_thread::sleep_for(50ms);
            return std::this_thread::get_id();
        });
        Check(worker != owner && frames >= 3, "Background preparation blocked presentation");
        try { platform::LoadingWork::Prepare([]() -> int { throw std::runtime_error("worker"); }); }
        catch (const std::runtime_error &) { ++frames; }
    }
    Check(!platform::LoadingWork::IsActive(), "Loading scope leaked");
    const auto path = std::filesystem::temp_directory_path() /
        ("PlutoGE-loading-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".plutoscene");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove(path, e); } } cleanup{path};
    {
        std::ofstream file(path);
        file << "SCENE\t1\n";
        for (int i=1; i<=2000; ++i)
            file << "ENTITY\t" << i << "\t0\t1\tFixture " << i << "\t0,0,0\t0,0,0\t1,1,1\n";
    }
    core::SceneLoading loading;
    std::unique_ptr<scene::Scene> active;
    std::vector<core::SceneLoadStage> stages;
    auto present = [&](const core::SceneLoadStatus &status)
    {
        Check(std::this_thread::get_id() == owner, "Scene construction left owner thread");
        Check(loading.IsLoading(), "Transition not marked active while presenting");
        stages.push_back(status.stage);
    };
    Check(loading.Load(path.string(), [&](auto scene)
    {
        Check(loading.Status().stage == core::SceneLoadStage::Activating, "Activation stage missing");
        Check(!loading.Load(path.string(), {}, present), "Nested transition accepted");
        active = std::move(scene);
    }, present), "Fixture load failed");
    Check(active && active->GetRootEntities().size() == 2000, "Fixture entities missing");
    Check(stages.front() == core::SceneLoadStage::Reading && stages.back() == core::SceneLoadStage::Complete,
          "Stage lifecycle missing");
    Check(!loading.IsLoading() && !platform::LoadingWork::IsActive(), "Transition lifetime leaked");
    auto *original = active.get();
    { std::ofstream file(path); file << "invalid header\n"; }
    bool activated = false;
    Check(!loading.Load(path.string(), [&](auto) { activated = true; }, present), "Invalid scene accepted");
    Check(!activated && active.get() == original && !loading.Status().error.empty(), "Failure replaced the old scene");
    Check(!loading.Activate([] { throw std::runtime_error("activation failed"); }, present), "Activation failure escaped");
    Check(loading.Activate([] {}, present), "Retry after failure failed");
    if (argc > 1)
    {
        auto &engine = core::Engine::GetInstance();
        core::EngineConfig config;
        config.windowConfig.title = "PlutoGE Loading Screen Fixture";
        config.windowConfig.width = 640;
        config.windowConfig.height = 360;
        config.windowConfig.visible = false;
        config.graphicsApi = std::string(argv[1]) == "--opengl"
            ? render::rhi::GraphicsApi::OpenGL : render::rhi::GraphicsApi::Vulkan;
        Check(engine.Initialize(config), "Graphics fixture initialization failed");
        const bool success = engine.GetSceneLoading().Activate([]
        {
            platform::LoadingWork::Prepare([] { std::this_thread::sleep_for(1500ms); return 0; });
        }, [&](const auto &status) { engine.PresentLoadingScreen(status); });
        const auto status = engine.GetSceneLoading().Status();
        const auto readPixels = [&]
        {
            const auto texture = engine.GetRhiRenderService().GetHostColorTexture();
            if (config.graphicsApi == render::rhi::GraphicsApi::Vulkan)
                return static_cast<render::rhi::vulkan::VulkanDevice &>(*engine.GetRenderDevice()).ReadTextureRgba8(texture);
            auto &device = static_cast<render::rhi::opengl::OpenGLDevice &>(*engine.GetRenderDevice());
            std::vector<std::byte> pixels(640*360*4);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(device.GetTextureNativeHandle(texture)));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        Check(engine.GetRhiRenderService().PresentLoading(0, 1), "First loading frame failed");
        const auto firstFrame = readPixels();
        Check(engine.GetRhiRenderService().PresentLoading(1, 3), "Second loading frame failed");
        const auto pixels = readPixels();
        Check(firstFrame != pixels, "Loading animation/stages did not change the rendered pixels");
        unsigned bright = 0;
        for (std::size_t i=0; i+3<pixels.size(); i+=4)
            if (std::to_integer<unsigned>(pixels[i]) > 100) ++bright;
        Check(bright > 100, "Loading label did not render");
        if (argc > 2)
        {
            std::ofstream image(argv[2], std::ios::binary);
            image << "P6\n640 360\n255\n";
            for (std::size_t row=0; row<360; ++row)
                for (std::size_t col=0; col<640; ++col)
                {
                    const auto y = config.graphicsApi == render::rhi::GraphicsApi::Vulkan ? row : 359-row;
                    image.write(reinterpret_cast<const char *>(pixels.data()+(y*640+col)*4), 3);
                }
        }
        std::cout << "Loading frames: " << status.presentedFrames << "; longest interval: " << status.longestFrameMs << " ms\n";
        engine.Shutdown();
        Check(success && status.presentedFrames >= 10, "Loading screen did not keep presenting");
    }
    std::cout << "PASS: worker isolation, responsive owner-thread pump, reentrancy, scene stages, activation, failures and retry\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
