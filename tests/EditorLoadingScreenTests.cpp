#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/platform/LoadingWork.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include "PlutoGE/ui/EditorViewportOverlay.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace PlutoGE;
static void Check(bool condition, const char *message)
{ if (!condition) throw std::runtime_error(message); }

class RecordingCompositor final : public ui::IEditorCompositor
{
public:
    bool Initialize(platform::Window &, render::rhi::IRenderDevice &, render::rhi::ISwapchain &) override { return true; }
    void Shutdown() override {}
    void BeginFrame() override { throw std::runtime_error("Loading started a new UI frame"); }
    void RenderDrawData(ImDrawData *data) override { current = data; }
    void RenderPlatformWindows(bool update) override { Check(!update, "Loading mutated platform window layout"); }
    ui::EditorTextureHandle RegisterTexture(const ui::EditorTextureDescriptor &) override { return {}; }
    void UpdateTexture(ui::EditorTextureHandle, const ui::EditorTextureDescriptor &) override {}
    void UnregisterTexture(ui::EditorTextureHandle) override {}
    std::uint64_t GetImGuiTextureId(ui::EditorTextureHandle) const noexcept override { return 0; }
    render::rhi::GraphicsApi GetGraphicsApi() const noexcept override { return render::rhi::GraphicsApi::OpenGL; }
    ImDrawData *current = nullptr;
};

static void TestOverlayContract()
{
    ImGui::CreateContext();
    {
        auto *viewport = ImGui::GetMainViewport();
        ImDrawData source;
        source.Valid = true;
        source.DisplaySize = {900, 600};
        source.FramebufferScale = {1, 1};
        source.OwnerViewport = viewport;
        viewport->DrawData = &source;
        ui::EditorViewportRegion region{viewport->ID, {220, 120}, {440, 260}};
        RecordingCompositor compositor;
        for (bool bottomUp : {false, true})
        {
            ui::PresentEditorViewportOverlay(compositor, region, 42, bottomUp, 900, 600, [&]
            {
                Check(compositor.current && compositor.current->CmdLists.Size == 1, "Overlay missing");
                const auto *list = compositor.current->CmdLists[0];
                Check(list->VtxBuffer.Size == 4 && list->IdxBuffer.Size == 6, "Unexpected overlay geometry");
                const auto &command = list->CmdBuffer[0];
                Check(command.ClipRect.x == 220 && command.ClipRect.y == 120 &&
                      command.ClipRect.z == 660 && command.ClipRect.w == 380, "Overlay escaped game viewport");
                Check(command.GetTexID() == 42, "Overlay used wrong texture");
                Check(list->VtxBuffer[0].uv.y == (bottomUp ? 1 : 0), "Texture orientation incorrect");
                Check(source.CmdLists.empty(), "Source editor frame was mutated");
            });
            Check(viewport->DrawData == &source && !compositor.current, "Temporary draw data leaked");
        }
        bool caught = false;
        try
        {
            ui::PresentEditorViewportOverlay(compositor, region, 42, false, 900, 600,
                [] { throw std::runtime_error("host failure"); });
        }
        catch (const std::runtime_error &) { caught = true; }
        Check(caught && viewport->DrawData == &source && !compositor.current, "Failed presentation leaked draw data");
        ui::PresentEditorViewportOverlay(compositor, {}, 0, false, 450, 300, [&]
        {
            Check(compositor.current->CmdLists.empty(), "Hidden game view produced an overlay");
            Check(compositor.current->DisplaySize.x == 450, "Resize retained stale framebuffer clipping");
        });
        viewport->DrawData = nullptr;
    }
    ImGui::DestroyContext();
}

static void TestGraphics(bool vulkan, bool detached, const char *capture)
{
    auto &engine = core::Engine::GetInstance();
    core::EngineConfig config;
    config.isEditorHost = true;
    config.windowConfig.title = "Editor loading viewport regression";
    config.windowConfig.width = 900;
    config.windowConfig.height = 600;
    config.windowConfig.visible = false;
    config.graphicsApi = vulkan ? render::rhi::GraphicsApi::Vulkan : render::rhi::GraphicsApi::OpenGL;
    Check(engine.Initialize(config), "Graphics initialization failed");
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    if (detached) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    auto compositor = ui::CreateEditorCompositor(config.graphicsApi);
    Check(compositor->Initialize(engine.GetWindow(), *engine.GetRenderDevice(), *engine.GetSwapchain()), "Compositor initialization failed");
    auto &service = engine.GetRhiRenderService();
    ui::EditorViewportRegion region;
    for (int frame = 0; frame < 4; ++frame)
    {
        engine.GetWindow().PollEvents();
        compositor->BeginFrame();
        ImGui::NewFrame();
        const auto origin = ImGui::GetMainViewport()->Pos;
        ImGui::SetNextWindowPos({origin.x, origin.y});
        ImGui::SetNextWindowSize({900, 600});
        ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::TextUnformatted("PLUTOGE EDITOR - VIEWPORT LOADING REGRESSION");
        ImGui::Separator();
        ImGui::TextUnformatted("Hierarchy                            Scene view                        Inspector");
        ImGui::End();
        ImGui::SetNextWindowPos({origin.x + (detached ? 940.0f : 220.0f), origin.y + 160});
        ImGui::SetNextWindowSize({500, 320});
        ImGui::Begin("Game Viewport", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        region = {ImGui::GetWindowViewport()->ID, ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail()};
        ImGui::Dummy(region.size);
        ImGui::End();
        ImGui::Render();
        if (!vulkan) { render::Graphics::ResetStateCache(); render::Graphics::Disable(GL_SCISSOR_TEST); engine.GetRenderer().BeginFrame(); }
        compositor->RenderDrawData(ImGui::GetDrawData());
        if (vulkan) Check(service.Present(), "Initial editor frame failed");
        else engine.GetRenderer().EndFrame();
        if (detached)
        {
            compositor->RenderPlatformWindows(true);
            for (auto *viewport : ImGui::GetPlatformIO().Viewports)
                if (viewport->PlatformHandle) glfwHideWindow(static_cast<GLFWwindow *>(viewport->PlatformHandle));
        }
    }
    if (detached) Check(region.platformViewport != ImGui::GetMainViewport()->ID, "Detached viewport fixture did not detach");
    const auto frameNumber = ImGui::GetFrameCount();
    auto *target = ImGui::FindViewportByID(region.platformViewport);
    auto *originalData = target->DrawData;
    const auto sourceLists = originalData->CmdLists.Size;
    const auto hostTexture = service.GetHostColorTexture();
    const auto width = static_cast<unsigned>(region.size.x);
    const auto height = static_cast<unsigned>(region.size.y);
    const render::LoadingScreenStyle style{"EMBERVAULT", {.95f, .48f, .12f}};
    Check(service.RenderLoading(width, height, 0, 1, style), "Offscreen loading render failed");
    Check(engine.GetSwapchain()->GetWidth() == 900 && engine.GetSwapchain()->GetHeight() == 600,
          "Offscreen loading resized the host window");
    Check(service.GetHostColorTexture() == hostTexture, "Loading replaced the editor host target");
    const auto registration = compositor->RegisterTexture({.device = engine.GetRenderDevice(),
        .texture = service.GetLoadingRenderer().GetColorTexture(), .width = width, .height = height});
    Check(registration.IsValid(), "Loading texture registration failed");
    const auto texture = static_cast<ImTextureID>(compositor->GetImGuiTextureId(registration));
    std::vector<unsigned char> baseline, loaded;
    const auto replay = [&](bool overlay, std::vector<unsigned char> *pixels)
    {
        if (!vulkan) { render::Graphics::ResetStateCache(); render::Graphics::Disable(GL_SCISSOR_TEST); engine.GetRenderer().BeginFrame(); }
        ui::PresentEditorViewportOverlay(*compositor, overlay ? region : ui::EditorViewportRegion{},
            overlay ? texture : 0, service.GetLoadingRenderer().IsTextureBottomUp(), 900, 600, [&]
        {
            if (vulkan) Check(service.Present(), "Loading editor composition failed");
            else
            {
                if (pixels)
                {
                    pixels->resize(900*600*4);
                    glReadPixels(0, 0, 900, 600, GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());
                }
                engine.GetRenderer().EndFrame();
            }
        });
    };
    replay(false, &baseline);
    {
        platform::LoadingWork pump([] {});
        for (int frame = 0; frame < 12; ++frame)
        {
            Check(service.RenderLoading(width, height, frame * .1f, 2, style), "Loading animation failed");
            replay(true, &loaded);
            Check(target->DrawData == originalData && originalData->CmdLists.Size == sourceLists, "Replay mutated editor layout");
            Check(ImGui::GetFrameCount() == frameNumber, "Loading re-entered the UI frame loop");
        }
    }
    if (!vulkan && detached) Check(loaded == baseline, "Detached loading changed the main editor window");
    if (!vulkan && !detached)
    {
        const auto origin = ImGui::GetMainViewport()->Pos;
        int changedInside = 0;
        int amberInside = 0;
        int brightOutside = 0;
        for (int y = 0; y < 600; ++y)
            for (int x = 0; x < 900; ++x)
            {
                const bool inside = x >= region.min.x-origin.x && x < region.min.x-origin.x+region.size.x &&
                                    y >= region.min.y-origin.y && y < region.min.y-origin.y+region.size.y;
                const auto offset = ((599-y)*900+x)*4;
                bool changed = false;
                for (int c = 0; c < 3; ++c) changed |= baseline[offset+c] != loaded[offset+c];
                if (inside)
                {
                    changedInside += changed;
                    amberInside += loaded[offset] > 160 && loaded[offset+1] > 50 && loaded[offset+2] < 80;
                }
                else
                {
                    Check(!changed, "Loading altered pixels outside the game viewport");
                    brightOutside += baseline[offset] > 160;
                }
            }
        Check(changedInside > 1000 && amberInside > 100, "Branded loading viewport did not render");
        Check(brightOutside > 100, "Surrounding editor UI did not render");
        if (capture)
        {
            std::ofstream image(capture, std::ios::binary);
            image << "P6\n900 600\n255\n";
            for (int y = 599; y >= 0; --y)
                for (int x = 0; x < 900; ++x)
                    image.write(reinterpret_cast<const char *>(loaded.data()+(y*900+x)*4), 3);
        }
        replay(false, &loaded);
        Check(loaded == baseline, "Loading overlay leaked into resumed editor frame");
    }
    bool rejected = false;
    try { engine.PresentLoadingScreen({}); }
    catch (const std::logic_error &) { rejected = true; }
    Check(rejected, "Editor accepted standalone whole-window loading presenter");
    compositor->UnregisterTexture(registration);
    compositor->Shutdown();
    ImGui::DestroyContext();
    engine.Shutdown();
}

int main(int argc, char **argv) try
{
    TestOverlayContract();
    if (argc > 1)
        TestGraphics(std::string(argv[1]) == "--vulkan", argc > 2 && std::string(argv[2]) == "--detached",
                     argc > 2 && std::string(argv[2]) != "--detached" ? argv[2] : nullptr);
    std::cout << "PASS: viewport clipping, orientation, frozen-frame replay, hidden view, resize and exception cleanup\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
