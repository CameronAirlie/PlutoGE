#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include <memory>
#include <stdexcept>

inline void CheckMeshReplacementRendering(PlutoGE::render::rhi::IRenderDevice &device,
                                         PlutoGE::render::BasicRendererShaderPackage shaders)
{
    using namespace PlutoGE::render;
    shaders.virtualShadows = {};
    RhiSceneRenderer renderer;
    if (!renderer.Initialize(device, shaders)) throw std::runtime_error("Scene renderer initialization failed");
    CameraData camera{glm::mat4(1), glm::mat4(1)};
    BasicLighting lighting;
    alignas(Mesh) std::byte storage[sizeof(Mesh)];
    // Same address, different geometry and index counts, exactly as an editing
    // session can produce after the allocator recycles generated road meshes.
    for (unsigned iteration = 0; iteration < 16; ++iteration)
    {
        MeshConfig config;
        config.data.vertices = {
            {{-.5f, -.5f, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
            {{ .5f, -.5f, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
            {{ 0, .5f, 0}, {0, 0, 1}, {.5f, 1}, {1, 0, 0, 1}}};
        for (unsigned triangle = 0; triangle <= iteration; ++triangle)
            config.data.indices.insert(config.data.indices.end(), {0, 1, 2});
        auto destroy = [](Mesh *mesh) { std::destroy_at(mesh); };
        std::unique_ptr<Mesh, decltype(destroy)> mesh(
            std::construct_at(reinterpret_cast<Mesh *>(storage), config), destroy);
        RenderCommand command;
        command.mesh = mesh.get();
        command.model = command.previousModel = glm::mat4(1);
        command.worldBounds = command.previousWorldBounds = mesh->GetBounds();
        for (unsigned frame = 0; frame < 2; ++frame)
        {
            if (!renderer.Render(64, 64, camera, lighting, std::span(&command, 1), {}))
                throw std::runtime_error("Replacement mesh render failed");
            if (renderer.GetTimingStats().meshUploadCount != (frame == 0 ? 1u : 0u))
                throw std::runtime_error("Mesh replacement used stale geometry or failed to cache live geometry");
        }
    }
    renderer.Shutdown();
}
