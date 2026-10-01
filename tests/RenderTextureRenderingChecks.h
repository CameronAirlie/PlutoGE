#pragma once
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/render/RhiRenderTextureRenderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace RenderTextureChecks
{
    // A CPU image texture, standing in for an imported screenshot.
    class PixelTexture final : public PlutoGE::render::Texture
    {
    public:
        PixelTexture(int width, int height, std::vector<unsigned char> pixels)
            : Texture(PlutoGE::render::TextureConfig{"screenshot.png"})
        {
            m_width = width;
            m_height = height;
            m_channels = 4;
            m_rgba8Pixels = std::move(pixels);
        }
    };
}

// A camera sees red above the horizon and green below. Shown on a material,
// its render texture must keep red above green on a quad with v = 1 at
// the top. Compare against a material-UV reference and check visible sides.
template <class Device>
void CheckRenderTextureMaterial(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const std::string &message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    const auto makeMesh = [](std::vector<std::array<float, 5>> vertices) {
        MeshConfig config;
        for (const auto &v : vertices)
            config.data.vertices.push_back({{v[0], v[1], v[2]}, {0, 0, 1}, {v[3], v[4]}, {1, 0, 0, 1}});
        for (std::uint32_t index = 0; index < config.data.vertices.size(); ++index)
            config.data.indices.push_back(index);
        return std::make_unique<Mesh>(config);
    };
    constexpr int size = 64;
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;

    // The camera's world: red sky above y = 0, green ground below.
    auto sky = makeMesh({{-10, 0, -5, 0, 0}, {10, 0, -5, 0, 0}, {10, 10, -5, 0, 0},
                         {-10, 0, -5, 0, 0}, {10, 10, -5, 0, 0}, {-10, 10, -5, 0, 0}});
    auto ground = makeMesh({{-10, -10, -5, 0, 0}, {10, -10, -5, 0, 0}, {10, 0, -5, 0, 0},
                            {-10, -10, -5, 0, 0}, {10, 0, -5, 0, 0}, {-10, 0, -5, 0, 0}});
    Material red({.color = {0, 0, 0, 1}, .emission = {1, 0, 0}});
    Material green({.color = {0, 0, 0, 1}, .emission = {0, 1, 0}});
    const std::array worldCommands{RenderCommand{.material = &red, .mesh = sky.get()},
                                   RenderCommand{.material = &green, .mesh = ground.get()}};
    const Camera camera(CameraConfig{.fovY = 90.0f, .nearPlane = 0.1f, .farPlane = 100.0f});
    const auto worldCamera = camera.GetCameraDataForTransform(glm::mat4(1), size, size);

    RenderTexture renderTexture("Monitor.plutorendertexture", {.width = size, .height = size});
    require(!renderTexture.GetGpuTexture(device), "A render texture has no image before it is rendered");
    const auto revisionBefore = renderTexture.GetContentRevision();
    RhiRenderTextureRenderer textureRenderer;
    const std::array views{RenderTextureView{&renderTexture, {.cameraData = worldCamera, .commands = worldCommands}}};
    require(textureRenderer.Render(device, views, {}, nullptr), "Render texture pass failed");
    require(static_cast<bool>(renderTexture.GetGpuTexture(device)), "Render texture image was not published");
    require(renderTexture.GetContentRevision() != revisionBefore, "Publishing must invalidate sampling materials");

    // Material UV space: v = 0 is the bottom (green), v = 1 the top (red).
    std::vector<unsigned char> pixels(size * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            auto *pixel = &pixels[(y * size + x) * 4];
            pixel[0] = y < size / 2 ? 0 : 255;
            pixel[1] = y < size / 2 ? 255 : 0;
            pixel[3] = 255;
        }
    RenderTextureChecks::PixelTexture screenshot(size, size, std::move(pixels));
    const auto readPixels = [](const Texture &texture) {
        const auto source = texture.GetRgba8Pixels();
        const auto *first = reinterpret_cast<const std::byte *>(source.data());
        return std::vector<std::byte>(first, first + source.size());
    };

    // A full-screen quad showing a texture as emission.
    auto screen = makeMesh({{-1, -1, .5f, 0, 0}, {1, -1, .5f, 1, 0}, {1, 1, .5f, 1, 1},
                            {-1, -1, .5f, 0, 0}, {1, 1, .5f, 1, 1}, {-1, 1, .5f, 0, 1}});
    const auto show = [&](Texture *texture) {
        Material material({.color = {0, 0, 0, 1}, .emission = {1, 1, 1}, .emissionTexture = texture});
        const std::array commands{RenderCommand{.material = &material, .mesh = screen.get()}};
        RhiSceneRenderer renderer;
        require(renderer.Initialize(device, shaders), "Display renderer initialization failed");
        const CameraData identity{.view = glm::mat4(1), .projection = glm::mat4(1)};
        require(renderer.Render(size, size, identity, lighting, commands, commands, {}, {}, readPixels),
                "Display render failed");
        return device.ReadTextureRgba8(renderer.GetColorTexture());
    };
    const auto expected = show(&screenshot);
    const auto actual = show(&renderTexture);

    const auto dominant = [](const std::vector<std::byte> &image, std::size_t pixel) {
        const int r = std::to_integer<int>(image[pixel * 4]);
        const int g = std::to_integer<int>(image[pixel * 4 + 1]);
        return r > 128 && g < 64 ? 'r' : g > 128 && r < 64 ? 'g' : '?';
    };
    std::size_t mismatches = 0, redPixels = 0, greenPixels = 0;
    // DisplayOutput is bottom-up on both backends, as used by the editor's
    // flipped display UVs. Assert orientation independently of the reference.
    require(dominant(actual, (size / 4) * size + size / 2) == 'g' &&
            dominant(actual, (size * 3 / 4) * size + size / 2) == 'r',
            "Render texture material shows the camera view upside down");
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(size * size); ++pixel)
    {
        const char want = dominant(expected, pixel);
        redPixels += want == 'r';
        greenPixels += want == 'g';
        mismatches += want != dominant(actual, pixel);
    }
    require(redPixels > size * size / 3 && greenPixels > size * size / 3, "Screenshot reference did not show both halves");
    // Allow the horizon row to differ by filtering.
    require(mismatches <= static_cast<std::size_t>(size * 2),
            "Render texture differs from the screenshot in " + std::to_string(mismatches) + " pixels");

    renderTexture.SetDescriptor({.width = size * 2, .height = size});
    require(renderTexture.GetWidth() == size * 2, "Resizing must update the texture size");
    require(textureRenderer.Render(device, views, {}, nullptr) && renderTexture.GetGpuTexture(device),
            "Resized render texture was not republished");
    textureRenderer.Shutdown();
    require(!renderTexture.GetGpuTexture(device), "Shutdown must unpublish destroyed images");
    std::cout << "Render texture matched its screenshot (" << mismatches << " filtered horizon pixels)" << std::endl;
}
