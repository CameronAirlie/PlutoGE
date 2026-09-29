#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
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
        for (unsigned frame = 0; frame < 4; ++frame)
        {
            if (frame == 2)
            {
                auto vertices = mesh->GetMeshData().vertices;
                vertices[0].position[0] -= .1f;
                const auto revision = mesh->GetContentRevision();
                mesh->UpdateVertexData(vertices);
                if (mesh->GetContentRevision() == revision) throw std::runtime_error("Mesh edit lost its revision");
            }
            if (!renderer.Render(64, 64, camera, lighting, std::span(&command, 1), {}))
                throw std::runtime_error("Replacement mesh render failed");
            if (renderer.GetTimingStats().meshUploadCount != (frame % 2 == 0 ? 1u : 0u))
                throw std::runtime_error("Mesh replacement used stale geometry or failed to cache live geometry");
        }
    }
    // Exercise the production scene queue, including inherited activation and
    // a settling frame. No engine window or runtime loop is required.
    MeshConfig config;
    config.data.vertices.push_back({{0,0,0}, {0,0,1}, {0,0}, {1,0,0,1}});
    config.data.indices = {0,0,0};
    Mesh mesh(config);
    Material material;
    auto &frontend = PlutoGE::core::Engine::GetInstance().GetRenderer();
    const auto require = [](bool value, const char *message) { if (!value) throw std::runtime_error(message); };
    {
        PlutoGE::scene::Scene scene;
        auto *parent = scene.AddEntity(std::make_unique<PlutoGE::scene::Entity>());
        auto *child = scene.AddEntity(std::make_unique<PlutoGE::scene::Entity>(), parent);
        auto *component = child->CreateComponent<PlutoGE::scene::MeshComponent>(PlutoGE::scene::MeshComponentConfig{.mesh=&mesh, .material=&material});
        const auto frame = [&] {
            frontend.ClearRenderCommands(); scene.SubmitRenderCommands();
            return frontend.GetSceneRenderCommandView();
        };
        require(frame().size() == 1, "Queued mesh was not registered");
        frame();
        require(frontend.GetRetainedSceneStats().updates == 0 && frontend.GetRetainedSceneStats().reusedProducers == 0,
            "Stationary scene still published mesh producers");
        parent->SetPosition({2,0,0});
        const auto moving = frame();
        require(moving[0].model[3].x == 2 && moving[0].previousModel[3].x == 0, "Ancestor motion history failed");
        require(frame()[0].previousModel[3].x == 2, "Settling history failed");
        parent->SetActive(false); require(frame().empty(), "Disabled ancestor retained child draw");
        parent->SetActive(true); require(frame().size() == 1, "Reactivated child not registered");
        component->SetEnabled(false); require(frame().empty(), "Disabled component retained draw");
        component->SetEnabled(true); require(frame().size() == 1, "Enabled component not registered");
        component->SetVisible(false); require(frame().empty(), "Hidden mesh retained draw");
        component->SetVisible(true); require(frame().size() == 1, "Shown mesh not registered");
        child->SetParent(nullptr); require(frame()[0].model[3].x == 0, "Reparented mesh used stale transform");
        scene.RemoveEntity(child); require(frame().empty(), "Removed mesh survived scene queue");
    }
    frontend.ClearRenderCommands(); require(frontend.GetSceneRenderCommandView().empty(), "Scene destruction retained draws");
    renderer.Shutdown();
}
