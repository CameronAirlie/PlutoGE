#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorSceneRenderService.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    using namespace render;
    const auto api = argc > 1 && std::string_view(argv[1]) == "--opengl"
        ? rhi::GraphicsApi::OpenGL : rhi::GraphicsApi::Vulkan;
    auto &engine = core::Engine::GetInstance();
    core::EngineConfig config; config.graphicsApi = api;
    config.windowConfig.visible = false; config.windowConfig.width = config.windowConfig.height = 64;
    if (!engine.Initialize(config)) throw std::runtime_error("Cannot initialize capture engine");
    struct Cleanup { core::Engine &engine; ~Cleanup() { engine.Shutdown(); } } cleanup{engine};
    scene::Scene scene;
    ui::EditorSceneRenderService service;
    if (!service.Initialize(api, engine.GetRenderDevice())) throw std::runtime_error("Cannot initialize capture service");
    constexpr glm::vec3 directions[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    constexpr glm::vec3 ups[] = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    constexpr glm::vec3 colors[] = {{2,0,0},{0,2,0},{0,0,2},{2,2,0},{2,0,2},{0,2,2}};
    std::array<std::unique_ptr<Mesh>,12> meshes;
    std::array<std::unique_ptr<Material>,12> materials;
    std::array<RenderCommand,12> commands;
    for (unsigned face = 0; face < 6; ++face)
    {
        const auto right = glm::cross(directions[face], ups[face]);
        for (unsigned half = 0; half < 2; ++half)
        {
            const auto index = face * 2 + half;
            MeshConfig mesh;
            for (glm::vec2 corner : {glm::vec2(-1,-1),glm::vec2(1,-1),glm::vec2(1,1),glm::vec2(-1,1)})
            {
                const auto position = directions[face]*2.0f +
                    (right*corner.x + ups[face]*(corner.y*.5f + (half == 0 ? -.5f : .5f)))*2.0f;
                const auto normal = -directions[face];
                mesh.data.vertices.push_back({{position.x,position.y,position.z},
                    {normal.x,normal.y,normal.z},{0,0},{1,0,0,1}});
            }
            mesh.data.indices = {0,1,2,0,2,3};
            meshes[index] = std::make_unique<Mesh>(mesh);
            MaterialConfig material; material.emission = colors[face] * (half == 0 ? 1.0f : 2.0f); material.twoSided = true; material.castsShadow = false;
            materials[index] = std::make_unique<Material>(material);
            commands[index].mesh = meshes[index].get(); commands[index].material = materials[index].get();
            commands[index].castsShadow = false;
            commands[index].worldBounds = commands[index].previousWorldBounds = meshes[index]->GetBounds();
        }
    }
    const auto pixels = service.CaptureIbl({0,0,0},32,10,commands,scene);
    if (pixels.size() != 32*32*24) throw std::runtime_error(service.GetLastRenderError());
    for (unsigned face = 0; face < 6; ++face)
    {
        for (unsigned half = 0; half < 2; ++half)
        {
            const auto at = (face*32*32 + (half == 0 ? 8 : 24)*32 + 16)*4;
            const glm::vec3 color(pixels[at],pixels[at+1],pixels[at+2]);
            const auto expected = colors[face] * (half == 0 ? 1.0f : 2.0f);
            for (unsigned channel = 0; channel < 3; ++channel)
                if (std::abs(color[channel] - expected[channel]) > .05f)
                    throw std::runtime_error("Capture lost HDR, face order, vertical orientation, or linear colour");
        }
    }
    if (!service.CaptureIbl({0,0,0},0,10,commands,scene).empty()) throw std::runtime_error("Invalid capture size accepted");
    std::cout << "PASS: six linear HDR IBL faces captured through RHI\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
