#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/UIRhiRenderer.h"
#include "PlutoGE/render/rhi/DeferredCommandList.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/ui/render/Painter.h"
#include "PlutoGE/ui/text/FontLibrary.h"
#include "PlutoGE/ui/text/RichText.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::render;
using namespace PlutoGE::render::rhi;

namespace
{
    constexpr int kWidth = 160;
    constexpr int kHeight = 96;
    // Background: (0.2, 0.4, 0.6) -> (51, 102, 153).
    constexpr int kBackground[3] = {51, 102, 153};

    using Reader = std::function<std::vector<std::byte>(TextureHandle, int, int)>;

    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Read-back rows are bottom-up, matching UI coordinates (y-up).
    struct Image
    {
        std::vector<std::byte> pixels;
        int Channel(int x, int y, int c) const
        {
            return std::to_integer<int>(pixels[(static_cast<std::size_t>(y) * kWidth + x) * 4 + c]);
        }
        std::string Describe(int x, int y) const
        {
            return "(" + std::to_string(Channel(x, y, 0)) + "," + std::to_string(Channel(x, y, 1)) + "," +
                   std::to_string(Channel(x, y, 2)) + "," + std::to_string(Channel(x, y, 3)) + ")";
        }
        bool Near(int x, int y, int r, int g, int b, int tolerance = 3) const
        {
            return std::abs(Channel(x, y, 0) - r) <= tolerance && std::abs(Channel(x, y, 1) - g) <= tolerance &&
                   std::abs(Channel(x, y, 2) - b) <= tolerance;
        }
        bool IsBackground(int x, int y) const { return Near(x, y, kBackground[0], kBackground[1], kBackground[2]); }
    };

    void RequirePixel(const Image &image, int x, int y, int r, int g, int b, const std::string &what, int tolerance = 3)
    {
        Require(image.Near(x, y, r, g, b, tolerance),
                what + ": pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " + image.Describe(x, y));
    }

    ui::DrawList BuildScene(std::unique_ptr<render::Texture> &quadrants)
    {
        ui::DrawList list;
        list.SetViewportClip({{0, 0}, {kWidth, kHeight}});
        // Opaque red square at the bottom-left: proves y-up placement.
        ui::DrawBox(list, {{10, 10}, {30, 30}}, {.fill = ui::Paint::Solid({1, 0, 0, 1})});
        // Rounded square: corners stay background.
        ui::DrawBox(list, {{40, 10}, {60, 30}}, {.fill = ui::Paint::Solid({1, 0, 0, 1}), .radii = ui::CornerRadii::All(10)});
        // 50% white: premultiplied blending.
        ui::DrawBox(list, {{70, 10}, {90, 30}}, {.fill = ui::Paint::Solid({1, 1, 1, 0.5f})});
        // Green border, transparent fill.
        ui::DrawBox(list, {{100, 10}, {120, 30}},
                    {.fill = ui::Paint::Solid({0, 0, 0, 0}), .borderWidth = 4, .borderColor = {0, 1, 0, 1}});
        // Rounded clip around a larger box.
        list.PushClip({{10, 40}, {30, 60}}, ui::CornerRadii::All(10));
        ui::DrawBox(list, {{0, 35}, {40, 65}}, {.fill = ui::Paint::Solid({1, 1, 0, 1})});
        list.PopClip();
        // Vertical gradient: black at the bottom, white at the top.
        ui::DrawBox(list, {{40, 40}, {60, 80}}, {.fill = ui::Paint::Linear({0, 0, 0, 1}, {1, 1, 1, 1}, 90)});
        // Texture orientation: quadrants red (top-left), green (top-right),
        // blue (bottom-left), white (bottom-right); rows are stored top first.
        const unsigned char texels[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        quadrants = render::Texture::CreateRgba8(2, 2, texels, "UI quadrants");
        ui::DrawImage(list, {{70, 40}, {90, 60}}, {.texture = ui::TextureKey::Asset("quadrants"), .textureSize = {2, 2}});
        // A large glyph.
        auto &fonts = ui::FontLibrary::Get();
        const auto content = ui::ParseRichText("I", {.font = fonts.Load(PLUTO_UI_TEST_FONT), .size = 40, .color = {1, 1, 1, 1}}, false);
        const auto layout = ui::LayoutText(content, {}, fonts, ui::GetTextShaper());
        ui::DrawText(list, layout, content, {130, 85}, {}, fonts.GetAtlas());
        return list;
    }

    Image RenderOnce(IRenderDevice &device, UIRhiRenderer &renderer, const ui::DrawList &list, const Reader &read,
                     bool deferred)
    {
        rhi::Texture target(device, device.CreateTexture({kWidth, kHeight, Format::R8G8B8A8Unorm, TextureUsage::ColorAttachment,
                                                          "Native UI test", true, 1, false, 1}));
        auto &commands = device.GetImmediateContext();
        const UIRhiRenderer::DrawItem item{&list, 1};
        DeferredCommandList captured(&commands);
        if (deferred)
            renderer.Render(target.Get(), kWidth, kHeight, captured, {&item, 1}, ui::FontLibrary::Get().GetAtlas());
        commands.BeginFrame("Native UI test");
        RenderingInfo clear;
        clear.colorAttachments = {target.Get()};
        clear.width = kWidth;
        clear.height = kHeight;
        clear.clearDepth = false;
        clear.clearColorValue[0] = 0.2f;
        clear.clearColorValue[1] = 0.4f;
        clear.clearColorValue[2] = 0.6f;
        clear.clearColorValue[3] = 1.0f;
        commands.BeginRendering(clear);
        commands.EndRendering();
        if (deferred)
            captured.Replay(commands);
        else
            renderer.Render(target.Get(), kWidth, kHeight, commands, {&item, 1}, ui::FontLibrary::Get().GetAtlas());
        commands.Submit();
        return {read(target.Get(), kWidth, kHeight)};
    }

    void Check(IRenderDevice &device, const Reader &read)
    {
        const ShaderArtifactLibrary shaders(PLUTO_RHI_TEST_SHADER_DIR);
        UIRhiRenderer renderer(device, shaders.Load("NativeUI", "vertex"), shaders.Load("NativeUI", "fragment"));
        Require(static_cast<bool>(renderer), "Native UI pipeline creation failed");
        std::unique_ptr<render::Texture> quadrants;
        const ui::DrawList list = BuildScene(quadrants);
        renderer.SetTextureResolver([&](const std::string &reference) { return reference == "quadrants" ? quadrants.get() : nullptr; });

        const Image image = RenderOnce(device, renderer, list, read, false);
        RequirePixel(image, 5, 5, kBackground[0], kBackground[1], kBackground[2], "Background outside the UI");
        RequirePixel(image, 20, 20, 255, 0, 0, "Opaque box (y-up placement)");
        RequirePixel(image, 50, 20, 255, 0, 0, "Rounded box centre");
        RequirePixel(image, 40, 10, kBackground[0], kBackground[1], kBackground[2], "Rounded box corner", 12);
        RequirePixel(image, 80, 20, 153, 178, 204, "Premultiplied 50% white", 4);
        RequirePixel(image, 101, 20, 0, 255, 0, "Border");
        RequirePixel(image, 110, 20, kBackground[0], kBackground[1], kBackground[2], "Transparent fill inside border");
        RequirePixel(image, 20, 50, 255, 255, 0, "Clip interior");
        RequirePixel(image, 5, 50, kBackground[0], kBackground[1], kBackground[2], "Outside the clip rect");
        RequirePixel(image, 10, 40, kBackground[0], kBackground[1], kBackground[2], "Rounded clip corner", 12);
        Require(image.Channel(50, 42, 0) < 40 && image.Channel(50, 78, 0) > 215, "Gradient runs bottom to top: " +
                image.Describe(50, 42) + " .. " + image.Describe(50, 78));
        RequirePixel(image, 72, 58, 255, 0, 0, "Image top-left texel", 6);
        RequirePixel(image, 88, 58, 0, 255, 0, "Image top-right texel", 6);
        RequirePixel(image, 72, 42, 0, 0, 255, "Image bottom-left texel", 6);

        int lit = 0;
        for (int y = 40; y < 90; ++y)
            for (int x = 128; x < 150; ++x)
                lit += image.Channel(x, y, 0) > 230 && image.Channel(x, y, 1) > 230 ? 1 : 0;
        Require(lit > 40, "Glyph did not render (" + std::to_string(lit) + " lit pixels)");

        // Unchanged geometry is reused; replay must match direct recording.
        Require(RenderOnce(device, renderer, list, read, true).pixels == image.pixels,
                "Deferred replay differs from direct recording");
        std::cout << "Native UI render checks passed\n";
    }
}

int main(int argc, char **argv)
try
{
    if (argc > 1 && std::string_view(argv[1]) == "--opengl")
    {
        platform::Window window;
        Require(window.Create({.title = "Native UI test", .width = kWidth, .height = kHeight, .resizable = false, .visible = false}),
                "Window creation failed");
        Require(window.EnsureOpenGLContextCurrent(true), "GL context unavailable");
        opengl::OpenGLDevice device;
        Check(device, [&](TextureHandle texture, int width, int height)
              {
                  std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4);
                  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(device.GetTextureNativeHandle(texture)));
                  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                  return pixels;
              });
    }
    else
    {
        vulkan::VulkanDevice device;
        Check(device, [&](TextureHandle texture, int, int) { return device.ReadTextureRgba8(texture); });
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
