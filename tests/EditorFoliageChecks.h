#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/render/RenderObjectIdentity.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// Retained instanced foliage (a FoliageComponent-like cluster: identity, revision, a LOD chain) draws the same through
// the editor's visibility pass (RenderSceneFrontend::PrepareVisibleRenderCommands) as handed straight to the RHI renderer like
// the runtime, and draws at all.
template <class Device>
void CheckEditorFoliage(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); };
    // A 4 x 4 grid of quads (LOD0) and one quad (LOD1), as one submesh with a chain.
    MeshConfig config;
    for (int y = 0; y <= 4; ++y)
        for (int x = 0; x <= 4; ++x)
            config.data.vertices.push_back({{x * 0.25f - 0.5f, y * 0.25f - 0.5f, 0.0f}, {0, 0, 1}, {x * 0.25f, y * 0.25f}, {1, 0, 0, 1}});
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
        {
            const unsigned a = y * 5 + x;
            config.data.indices.insert(config.data.indices.end(), {a, a + 1, a + 6, a, a + 6, a + 5});
        }
    const auto lod0 = static_cast<std::uint32_t>(config.data.indices.size());
    config.data.indices.insert(config.data.indices.end(), {0u, 4u, 24u, 0u, 24u, 20u});
    Submesh submesh;
    submesh.indexCount = lod0;
    submesh.bounds = {glm::vec3(0.0f), 0.71f};
    submesh.lods = {{0, lod0, 0.0f, std::numeric_limits<float>::max()}, {lod0, 6, 8.0f, 4.0f}};
    config.submeshes = {submesh};
    auto mesh = std::make_unique<Mesh>(config);
    Material material({.emission = {1, 1, 1}});

    auto instances = std::make_shared<std::vector<glm::mat4>>();
    for (int i = 0; i < 40; ++i) // a row across the view and beyond it
        instances->push_back(glm::translate(glm::mat4(1), glm::vec3(float(i) * 0.6f - 12.0f, 0.0f, -6.0f)));
    RenderCommand cluster{.material = &material, .mesh = mesh.get()};
    cluster.instanceModels = instances;
    cluster.previousInstanceModels = instances;
    cluster.model = instances->front();
    cluster.worldBounds = {glm::vec3(0, 0, -6), 13.0f};
    cluster.isStatic = true;
    cluster.sourceObject = RenderObjectIdentity().Value();
    cluster.sourceRevision = 1;
    std::vector<RenderCommand> commands{cluster};

    CameraData camera{.view = glm::mat4(1), .projection = glm::perspective(glm::radians(60.0f), 1.0f, 1000.0f, 0.1f)};
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    lighting.shadowsEnabled = false;
    RhiSceneRenderer renderer;
    require(renderer.Initialize(device, shaders), "Editor foliage renderer initialization failed");
    const auto render = [&](RenderCommandView visible, RenderCommandView scene)
    {
        for (int frame = 0; frame < 2; ++frame)
            require(renderer.Render(64, 64, camera, lighting, visible, scene), "Editor foliage render failed");
        return device.ReadTextureRgba8(renderer.GetColorTexture());
    };
    const auto runtime = render(commands, commands);
    RenderSceneFrontend editor;
    for (const auto &command : commands) editor.SubmitRenderCommand(command);
    editor.PrepareVisibleRenderCommands(camera, 64);
    const auto viaEditor = render(editor.GetVisibleRenderCommandView(), editor.GetSceneRenderCommandView());
    std::size_t lit = 0;
    for (std::size_t texel = 0; texel + 3 < runtime.size(); texel += 4) lit += static_cast<unsigned>(runtime[texel]) > 64u;
    std::cout << "Editor foliage: " << lit << " lit pixels; editor path " << (viaEditor == runtime ? "matches" : "differs") << '\n';
    require(lit > 0, "Instanced foliage drew nothing");
    require(viaEditor == runtime, "Instanced foliage drew differently through the editor's visibility pass");
}
