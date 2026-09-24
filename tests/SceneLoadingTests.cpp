#include "PlutoGE/core/LoadingScreenSession.h"
#include "PlutoGE/assets/LoadingScreenAsset.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include <sstream>
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
    assets::LoadingScreenAsset asset{"project://UI/loading.rml", "Game.Loader"};
    std::stringstream serialized;
    Check(assets::WriteLoadingScreenAsset(serialized, asset), "Valid asset rejected");
    assets::LoadingScreenAsset parsed;
    Check(assets::ReadLoadingScreenAsset(serialized, parsed) && parsed.document == asset.document &&
        parsed.controller == asset.controller, "Loading asset round trip failed");
    for (const auto *invalid : {"LoadingScreenVersion=2\nDocument=project://UI/a.rml\n",
        "LoadingScreenVersion=1\nDocument=project://../a.rml\n",
        "LoadingScreenVersion=1\nDocument=project://UI/a.rml\nDocument=project://UI/b.rml\n",
        "LoadingScreenVersion=1\nDocument=project://UI/a.png\n"})
    {
        std::istringstream input(invalid);
        Check(!assets::ReadLoadingScreenAsset(input, parsed) && parsed.document == asset.document,
            "Invalid asset accepted or partially applied");
    }
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
        const auto readTexture = [&](render::rhi::TextureHandle texture)
        {
            if (config.graphicsApi == render::rhi::GraphicsApi::Vulkan)
                return static_cast<render::rhi::vulkan::VulkanDevice &>(*engine.GetRenderDevice()).ReadTextureRgba8(texture);
            auto &device = static_cast<render::rhi::opengl::OpenGLDevice &>(*engine.GetRenderDevice());
            std::vector<std::byte> pixels(640*360*4);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(device.GetTextureNativeHandle(texture)));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        const auto readPixels = [&] { return readTexture(engine.GetRhiRenderService().GetLoadingRenderer().GetColorTexture()); };
        Check(engine.GetRhiRenderService().PresentLoading(0, 1), "First loading frame failed");
        const auto firstFrame = readPixels();
        Check(engine.GetRhiRenderService().PresentLoading(1, 3), "Second loading frame failed");
        const auto pixels = readPixels();
        Check(firstFrame != pixels, "Loading animation/stages did not change the rendered pixels");
        unsigned bright = 0;
        for (std::size_t i=0; i+3<pixels.size(); i+=4)
            if (std::to_integer<unsigned>(pixels[i]) > 100) ++bright;
        Check(bright > 100, "Loading label did not render");
        render::LoadingScreenStyle branded;
        branded.title = "F";
        branded.accent = {.95f, .48f, .12f};
        Check(engine.GetRhiRenderService().PresentLoading(1, 3, branded), "Orientation fixture failed");
        const auto orientation = readPixels();
        const auto red = [&](int x, int y) { return std::to_integer<unsigned>(orientation[(y*640+x)*4]); };
        Check(red(331, 274) > 180 && red(331, 240) < 80,
            "Loading glyph F is flipped: top-right bar must be above the stem");
        branded.title = "EMBERVAULT";
        branded.accent = {.95f, .48f, .12f};
        Check(engine.GetRhiRenderService().PresentLoading(1, 3, branded), "Custom loading frame failed");
        const auto brandedPixels = readPixels();
        Check(brandedPixels != pixels, "Project loading style did not change the screen");
        Check(engine.GetRhiRenderService().PresentLoading(1, 3), "Default loading restore failed");
        Check(readPixels() == pixels, "Project branding leaked into the default screen");
        if (argc > 2)
        {
            std::ofstream image(argv[2], std::ios::binary);
            image << "P6\n640 360\n255\n";
            for (std::size_t row=0; row<360; ++row)
                for (std::size_t col=0; col<640; ++col)
                {
                    const auto y = 359-row;
                    image.write(reinterpret_cast<const char *>(brandedPixels.data()+(y*640+col)*4), 3);
                }
        }
        const auto fixture = std::filesystem::temp_directory_path() /
            ("PlutoGE-loading-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(fixture / "Assets");
        struct FixtureCleanup { std::filesystem::path path; ~FixtureCleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } fixtureCleanup{fixture};
        engine.GetAssetManager().SetProjectContext(fixture.string());
        { std::ofstream file(fixture / "Assets/test.plutoloading");
          Check(assets::WriteLoadingScreenAsset(file, {"project://test.rml", "Test.Loading"}), "Fixture asset write failed"); }
        { std::ofstream file(fixture / "Assets/test.rml"); file << R"(<rml><head><link type="text/rcss" href="test.rcss"/></head><body>
            <div id="top"/><div id="bottom"/><div id="spin"/><div id="flag"/>
            <div id="loading-stage"/><div id="loading-error"/></body></rml>)"; }
        { std::ofstream file(fixture / "Assets/test.rcss"); file << R"(
            body { width: 100%; height: 100%; margin: 0; background-color: #101820; }
            #top { position: absolute; top: 0; left: 0; width: 80px; height: 40px; background-color: #ff0000; }
            #bottom { position: absolute; bottom: 0; left: 0; width: 80px; height: 40px; background-color: #00ff00; }
            #flag { position: absolute; top: 0; right: 0; width: 40px; height: 40px; }
            #spin { position: absolute; top: 100px; left: 300px; width: 40px; height: 10px; background-color: #ffffff; animation: 1s linear infinite turn; }
            #loading-stage, #loading-error { display: none; }
            @keyframes turn { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
        )"; }
        int creates = 0, updates = 0, destroys = 0;
        struct Controller : scripting::ScriptInstance
        {
            int &creates, &updates, &destroys;
            Controller(int &a, int &b, int &c) : creates(a), updates(b), destroys(c) {}
            void OnCreate() override { ++creates; }
            void OnUpdate(float) override
            {
                ++updates;
                auto &ui = render::RmlUiRuntime::Get();
                Check(ui.GetElementText("loading://active", "loading-stage") == "Reading scene", "Stage binding missing");
                Check(ui.SetElementStyle("loading://active", "flag", "background-color", "#0000ff"), "Controller cannot access loading document");
            }
            void OnDestroy() override { ++destroys; }
        };
        scripting::ScriptClassDefinition controller;
        controller.namespaceName = "Test"; controller.className = "Loading";
        controller.assignableTypeNames = {"PlutoGE.ScriptCore.LoadingScreenController"};
        engine.GetScriptEngine().RegisterNativeClass(controller, [&] { return std::make_unique<Controller>(creates, updates, destroys); });
        render::LoadingScreenStyle custom; custom.assetReference = "project://test.plutoloading";
        {
            core::LoadingScreenSession session(engine, custom);
            core::SceneLoadStatus reading; reading.stage = core::SceneLoadStage::Reading;
            Check(session.Render(reading, 640, 360) && session.GetError().empty(), "Custom RML failed to render");
            const auto before = readTexture(session.GetTexture());
            Check(std::to_integer<unsigned>(before[((359-20)*640+20)*4]) > 200 &&
                  std::to_integer<unsigned>(before[(20*640+20)*4+1]) > 200,
                  "RML document is upside down");
            Check(std::to_integer<unsigned>(before[((359-20)*640+620)*4+2]) > 200, "Controller style did not render");
            render::RmlUiRuntime::Get().ResetRuntimeState();
            std::this_thread::sleep_for(150ms);
            Check(session.Render(reading, 640, 360) && session.GetError().empty(), "Scene reset destroyed loading UI");
            Check(readTexture(session.GetTexture()) != before, "RCSS animation did not advance");
            Check(creates == 1 && updates == 2 && destroys == 0, "Controller lifetime incorrect");
        }
        Check(destroys == 1, "Controller not destroyed with transition");
        Check(!render::RmlUiRuntime::Get().SetElementText("loading://active", "loading-stage", "stale"), "Loading alias escaped callback scope");
        { core::LoadingScreenSession preview(engine, custom, false);
          Check(preview.Render({}, 640, 360), "Preview failed"); }
        Check(creates == 1 && destroys == 1, "Preview ran project script");
        { custom.assetReference = "project://missing.plutoloading";
          core::LoadingScreenSession fallback(engine, custom);
          Check(fallback.Render({}, 640, 360) && !fallback.GetError().empty(), "Broken asset did not fall back"); }
        struct BrokenController : scripting::ScriptInstance
        {
            void OnCreate() override { throw std::runtime_error("Expected controller initialization failure"); }
            void OnDestroy() override { render::RmlUiRuntime::Get().ShowDocument("loading://active", false); }
        };
        controller.className = "Broken";
        engine.GetScriptEngine().RegisterNativeClass(controller, [] { return std::make_unique<BrokenController>(); });
        { std::ofstream file(fixture / "Assets/broken.plutoloading");
          Check(assets::WriteLoadingScreenAsset(file, {"project://test.rml", "Test.Broken"}), "Broken controller fixture write failed"); }
        {
            custom.assetReference = "project://broken.plutoloading";
            core::LoadingScreenSession broken(engine, custom);
            Check(broken.Render({}, 640, 360) && !broken.GetError().empty(), "Controller initialization error was not contained");
            const auto image = readTexture(broken.GetTexture());
            Check(std::to_integer<unsigned>(image[((359-20)*640+20)*4]) > 200,
                "Controller cleanup hid the authored loading screen");
        }
        if (argc > 3)
        {
            std::string error;
            auto project = assets::Project::Load(argv[3], &error);
            Check(project != nullptr, "Project loading fixture manifest could not load");
            engine.GetAssetManager().SetProjectContext(project->GetRootDirectory().string(), project->GetManifest().assetDirectory);
            Check(engine.GetScriptEngine().LoadAssembly(project->ResolveAssetReference(project->GetManifest().scriptAssembly)),
                "Project managed assembly could not load");
            Check(!engine.GetScriptEngine().GetLoadingScreenClassNames().empty(), "Managed loading controller was not discovered");
            core::LoadingScreenSession projectScreen(engine, project->GetManifest().loadingScreen);
            core::SceneLoadStatus reading; reading.stage = core::SceneLoadStage::Reading;
            for (int i = 0; i < 3; ++i)
            {
                Check(projectScreen.Render(reading, 640, 360) && projectScreen.GetError().empty(), "Project RML/controller failed");
                std::this_thread::sleep_for(50ms);
            }
            const auto imagePixels = readTexture(projectScreen.GetTexture());
            std::ofstream image(std::string(argv[2]) + ".custom.ppm", std::ios::binary);
            image << "P6\n640 360\n255\n";
            for (int y = 359; y >= 0; --y)
                for (int x = 0; x < 640; ++x)
                    image.write(reinterpret_cast<const char *>(imagePixels.data() + (y*640+x)*4), 3);
        }
        engine.GetAssetManager().ClearProjectContext();
        std::cout << "Loading frames: " << status.presentedFrames << "; longest interval: " << status.longestFrameMs << " ms\n";
        engine.Shutdown();
        Check(success && status.presentedFrames >= 10, "Loading screen did not keep presenting");
    }
    std::cout << "PASS: worker isolation, responsive owner-thread pump, reentrancy, scene stages, activation, failures and retry\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
