#include "PlutoGE/ui/RmlDocumentEditSession.h"
#include "PlutoGE/ui/RmlDocumentPreview.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/RmlLoadingDocument.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include <RmlUi/Core.h>
#include <glad/glad.h>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char *message)
    { if (!condition) throw std::runtime_error(message); }
    struct Fixture
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-rml-preview-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture() { std::filesystem::create_directories(root / "UI"); }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    };
}

int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    const bool vulkan = argc > 1 && std::string_view(argv[1]) == "--vulkan";
    Fixture fixture;
    const auto path = fixture.root / "UI" / "screen.rml";
    const auto stylesheet = fixture.root / "UI" / "theme.rcss";
    std::filesystem::create_directories(fixture.root / "UI" / "Images");
    std::filesystem::create_directories(fixture.root / "UI" / "Fonts");
    std::filesystem::copy_file(PLUTO_RML_TEST_FONT, fixture.root / "UI" / "Fonts" / "Preview.ttf");
    // The RHI RmlUi renderer accepts uncompressed TGA images.
    std::array<unsigned char, 30> image{};
    image[2] = 2; image[12] = image[14] = 2; image[16] = 24; image[17] = 32;
    for (std::size_t pixel = 18; pixel < image.size(); pixel += 3) { image[pixel+1] = image[pixel+2] = 255; }
    std::ofstream imageFile(fixture.root / "UI" / "Images" / "icon.tga", std::ios::binary);
    imageFile.write(reinterpret_cast<const char *>(image.data()), image.size()); imageFile.close();
    const std::string markup = "<rml><head><link type='text/rcss' href='theme.rcss'/></head><body><div id='box'/><img src='Images/icon.tga' style='position: absolute; left: 40px; top: 0; width: 16px; height: 16px;'/><span style='position: absolute; left: 0; top: 40px; font-family: PreviewFont; font-size: 14px; color: white;'>A</span></body></rml>";
    const std::string red = "body { margin: 0; } #box { display: block; width: 32px; height: 32px; background-color: #ff0000; }";
    std::ofstream(path) << markup;
    std::ofstream(stylesheet) << red;
    auto &engine = core::Engine::GetInstance();
    core::EngineConfig config;
    config.isEditorHost = true;
    config.graphicsApi = vulkan ? render::rhi::GraphicsApi::Vulkan : render::rhi::GraphicsApi::OpenGL;
    config.windowConfig.visible = false;
    config.windowConfig.width = 128; config.windowConfig.height = 128;
    Check(engine.Initialize(config), "Engine initialization failed");
    struct EngineScope { core::Engine &engine; ~EngineScope() { engine.Shutdown(); } } engineScope{engine};
    auto &runtime = render::RmlUiRuntime::Get();
    auto *device = engine.GetRenderDevice();
    auto original = runtime.CreateLoadingDocument(path.generic_string(), engine.GetWindow(), *device);
    Check(original != nullptr, "Disk document failed to load");
    ui::RmlDocumentEditSession session;
    session.Open(path, fixture.root);
    session.SetSource(1, "@font-face { font-family: PreviewFont; src: url('Fonts/Preview.ttf'); } body { margin: 0; } #box { display: block; width: 32px; height: 32px; background-color: #0000ff; }");
    ui::RmlDocumentPreview preview;
    const bool built = preview.Rebuild(engine, session);
    const bool rendered = built && preview.Render(engine, 64, 64, {0, 1, 0, 1});
    if (!rendered) for (const auto &message : preview.GetDiagnostics()) std::cerr << message << '\n';
    Check(rendered, "Unsaved preview failed");
    const auto read = [&]()
    {
        if (vulkan) return static_cast<render::rhi::vulkan::VulkanDevice *>(device)->ReadTextureRgba8(preview.GetTexture());
        std::vector<std::byte> pixels(64 * 64 * 4);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(static_cast<render::rhi::opengl::OpenGLDevice *>(device)->GetTextureNativeHandle(preview.GetTexture())));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    const auto pixels = read();
    const auto channel = [&](int x, int y, int c) { return std::to_integer<int>(pixels[((63-y)*64+x)*4+c]); };
    if (channel(8, 8, 2) <= 240 || channel(8, 8, 0) >= 10)
        std::cerr << "Box pixel: " << channel(8, 8, 0) << ',' << channel(8, 8, 1) << ',' << channel(8, 8, 2) << '\n';
    Check(channel(8, 8, 2) > 240 && channel(8, 8, 0) < 10, "Unsaved RCSS did not render blue");
    Check(channel(48, 8, 0) > 240 && channel(48, 8, 1) > 240 && channel(48, 8, 2) < 10, "Relative preview image did not load");
    int glyphPixels = 0;
    for (int y = 40; y < 60; ++y) for (int x = 0; x < 20; ++x) glyphPixels += channel(x, y, 0) > 80 && channel(x, y, 2) > 80;
    Check(glyphPixels > 5, "Font declared in unsaved RCSS did not load relative to its stylesheet");
    Check(channel(48, 48, 1) > 240, "Preview background was not retained");
    auto diskAfterPreview = runtime.CreateLoadingDocument(path.generic_string(), engine.GetWindow(), *device);
    Check(diskAfterPreview && diskAfterPreview->GetDocument()->GetElementById("box")->GetProperty("background-color")->Get<Rml::Colourb>().red == 255,
        "Preview stylesheet leaked into subsequent disk document");
    Check(original->GetDocument()->GetElementById("box")->GetProperty("background-color")->Get<Rml::Colourb>().red == 255,
        "Preview modified an existing document");
    runtime.ResetRuntimeState();
    Check(preview.Render(engine, 64, 64, {0, 1, 0, 1}), "Scene reset destroyed independent preview");
    const auto previous = preview.GetTexture();
    session.SetSource(0, "<rml><body><div></span></body></rml>");
    Check(!preview.Rebuild(engine, session) && preview.IsStale() && !preview.GetDiagnostics().empty(), "Invalid source replaced valid preview");
    Check(preview.GetTexture() == previous && preview.Render(engine, 64, 64, {0, 1, 0, 1}), "Last valid preview was lost");
    session.SetSource(0, markup);
    Check(preview.Rebuild(engine, session) && preview.Render(engine, 120, 90, {0, 0, 0, 1}), "Preview did not recover or resize");
    const auto frame = fixture.root / "UI" / "frame.rml";
    std::ofstream(frame) << "<template name='frame' content='host'><head></head><body><div id='disk-frame'/><div id='host'/></body></template>";
    const std::string templated = "<rml><head><link type='text/template' href='frame.rml'/></head><body template='frame'><div id='box'/></body></rml>";
    session.SetSource(0, templated);
    session.RefreshDependencies();
    const auto &buffers = session.GetBuffers();
    const auto frameBuffer = std::find_if(buffers.begin(), buffers.end(), [&](const auto &buffer) { return buffer.path == std::filesystem::weakly_canonical(frame); });
    Check(frameBuffer != buffers.end(), "Linked template buffer missing");
    session.SetSource(static_cast<std::size_t>(frameBuffer - buffers.begin()),
        "<template name='frame' content='host'><head></head><body><div id='preview-frame'/><div id='host'/></body></template>");
    const bool templateBuilt = preview.Rebuild(engine, session);
    if (!templateBuilt) for (const auto &message : preview.GetDiagnostics()) std::cerr << message << '\n';
    Check(templateBuilt && preview.Render(engine, 64, 64, {0, 0, 0, 1}), "Unsaved template preview failed");
    std::vector<std::string> templateDiagnostics;
    auto overlayDocument = runtime.CreatePreviewDocument(path.generic_string(), session.GetSourceOverlay(), engine.GetWindow(), *device, templateDiagnostics);
    Check(overlayDocument && overlayDocument->GetDocument()->GetElementById("preview-frame") && overlayDocument->GetDocument()->GetElementById("box"),
        "Template source overlay did not inject the preview body");
    std::ofstream(path) << templated;
    auto diskTemplate = runtime.CreateLoadingDocument(path.generic_string(), engine.GetWindow(), *device);
    Check(diskTemplate && diskTemplate->GetDocument()->GetElementById("disk-frame") && !diskTemplate->GetDocument()->GetElementById("preview-frame"),
        "Unsaved template leaked into runtime cache");
    overlayDocument.reset(); diskTemplate.reset();
    preview.Reset(); diskAfterPreview.reset(); original.reset();
    std::cout << "RML preview isolation, recovery and rendering passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
