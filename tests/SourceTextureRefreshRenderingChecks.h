#pragma once
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderCommand.h"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

template<class Reader>
void CheckSourceTextureRefreshRendering(PlutoGE::render::rhi::IRenderDevice &device,
    PlutoGE::render::BasicRendererShaderPackage shaders, Reader read)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value, const char *message) { if (!value) throw std::runtime_error(message); };
    const auto parent = std::filesystem::temp_directory_path();
    const auto path = parent / ("PlutoGE-texture-render-refresh-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ppm");
    struct Cleanup
    {
        std::filesystem::path path, parent;
        ~Cleanup() { if (path.parent_path() == parent && path.filename().string().starts_with("PlutoGE-texture-render-refresh-"))
            { std::error_code error; std::filesystem::remove(path, error); } }
    } cleanup{path,parent};
    const auto write = [&](std::array<unsigned char,3> color)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "P6\n1 1\n255\n";
        output.write(reinterpret_cast<const char *>(color.data()), color.size()); output.close();
        require(bool(output), "Cannot write source texture render fixture");
    };
    write({255,0,0});
    TextureManager textures(true); // CPU source pixels; each RHI owns its upload.
    auto *texture = textures.LoadTextureFromFile(path.string().c_str(), TextureColorSpace::SRGB);
    require(texture != nullptr, "Cannot decode source texture render fixture");
    Material material({.albedoTexture=texture, .castsShadow=false, .twoSided=true});
    MeshConfig config;
    config.data.vertices = {{{-1,-1,.5f},{0,0,1},{0,0},{1,0,0,1}},
        {{3,-1,.5f},{0,0,1},{0,0},{1,0,0,1}}, {{-1,3,.5f},{0,0,1},{0,0},{1,0,0,1}}};
    config.data.indices = {0,1,2};
    Mesh mesh(config);
    RenderCommand command;
    command.mesh=&mesh; command.material=&material; command.castsShadow=false;
    command.worldBounds = command.previousWorldBounds = mesh.GetBounds();
    shaders.virtualShadows = {};
    RhiSceneRenderer renderer;
    require(renderer.Initialize(device, shaders), "Cannot initialize source texture scene renderer");
    BasicLighting lighting; lighting.shadowsEnabled=false;
    CameraData camera{glm::mat4(1),glm::mat4(1)};
    const auto pixels = [](const Texture &source)
    {
        const auto bytes = std::as_bytes(source.GetRgba8Pixels());
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    const auto frame = [&](unsigned red, unsigned blue)
    {
        require(renderer.Render(32,32,camera,lighting,std::span(&command,1),{},{},{},pixels,PostProcessDebugView::Albedo),
            "Source texture scene render failed");
        const auto image = read(renderer.GetColorTexture(),32u,32u);
        require(image.size() == 32u*32u*4u, "Source texture readback size differs");
        const auto center = (16u*32u+16u)*4u;
        require(std::abs(int(std::to_integer<unsigned char>(image[center]))-int(red)) <= 3 &&
            std::abs(int(std::to_integer<unsigned char>(image[center+2]))-int(blue)) <= 3,
            "Production renderer retained stale source texture pixels");
    };
    frame(255,0);
    require(renderer.GetTimingStats().textureUploadCount > 0, "Initial source texture was not uploaded");
    frame(255,0);
    require(renderer.GetTimingStats().textureUploadCount == 0, "Stable source texture was uploaded again");
    const auto identity = texture->GetIdentity();
    write({0,0,255});
    std::string error;
    require(textures.ReloadFileTexture(path.string(), &error), error.c_str());
    require(texture->GetIdentity() == identity && material.ReadConfig().albedoTexture == texture, "Refresh replaced a source texture borrower");
    frame(0,255);
    require(renderer.GetTimingStats().textureUploadCount > 0, "Content revision did not invalidate the RHI source upload");
    frame(0,255);
    require(renderer.GetTimingStats().textureUploadCount == 0, "Refreshed source texture did not settle in the cache");
    { std::ofstream invalid(path, std::ios::binary | std::ios::trunc); invalid << "invalid"; }
    require(!textures.ReloadFileTexture(path.string(), &error), "Invalid image replaced the RHI source texture");
    frame(0,255);
    require(renderer.GetTimingStats().textureUploadCount == 0, "Failed image refresh invalidated the RHI upload");
    renderer.Shutdown();
}
