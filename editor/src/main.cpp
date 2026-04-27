#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>

int main(int argc, char **argv)
{
    auto &engine = PlutoGE::core::Engine::GetInstance();

    if (!engine.Initialize())
    {
        return -1;
    }

    auto &window = engine.GetWindow();
    const auto &input = window.GetInputState();
    auto &renderer = engine.GetRenderer();

    auto scene = std::make_unique<PlutoGE::scene::Scene>();

    auto entity = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{
        .name = "CubeEntity",
        .tags = {"Player", "Enemy"}});

    auto positionX = 0.0f;
    entity->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));

    auto material = engine.GetAssetManager().CreateDefaultMaterial();
    auto texturedMaterial = engine.GetAssetManager().CreateDefaultMaterial();
    texturedMaterial->SetAlbedoTexture(engine.GetAssetManager().LoadTexture("U:\\Documents\\cobble\\cobble_base.png"));
    texturedMaterial->SetNormalTexture(engine.GetAssetManager().LoadTexture("U:\\Documents\\cobble\\cobble_normal.png"));
    texturedMaterial->SetColor(glm::vec3(1.0f, 1.0f, 1.0f)); // Set a base color for the material

    auto mesh = PlutoGE::render::Mesh::Cube();
    auto meshComponent = std::make_unique<PlutoGE::scene::MeshComponent>(PlutoGE::scene::MeshComponentConfig{
        .mesh = mesh,
        .material = texturedMaterial});
    entity->AddComponent(meshComponent.release());
    scene->AddEntity(entity.release());

    auto camera = PlutoGE::render::Camera(PlutoGE::render::CameraConfig{
        .fovY = 90.0f,
        .aspectRatio = 800.0f / 600.0f,
        .nearPlane = 0.1f,
        .farPlane = 100.0f,
    });
    glm::vec3 cameraPos(0.0f, 0.0f, 5.0f);

    renderer.SetCamera(&camera);

    auto modelMatrix = glm::mat4(1.0f); // Identity model matrix

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastTime = startTime;
    auto deltaTime = std::chrono::duration<float>(0.0f);

    glm::vec3 direction = glm::vec3(0.0f, 0.0f, 0.0f);

    auto cube = scene->FindEntityByName("CubeEntity");

    while (!window.ShouldClose())
    {
        camera.SetViewMatrix(glm::lookAt(
            cameraPos,                                // Camera position
            cameraPos + glm::vec3(0.0f, 0.0f, -1.0f), // Target (looking forward)
            glm::vec3(0.0f, 1.0f, 0.0f)               // Up
            ));

        auto currentTime = std::chrono::high_resolution_clock::now();
        deltaTime = currentTime - lastTime;

        cube->SetRotation(glm::vec3(0.0f, positionX * 50.0f, 0.0f));

        scene->Update(deltaTime.count());

        positionX += 0.5f * deltaTime.count(); // Move at 0.5 units per second

        renderer.BeginFrame();

        renderer.RenderFrame();

        renderer.EndFrame();

        window.PollEvents();

        lastTime = currentTime;
    }

    return 0;
}