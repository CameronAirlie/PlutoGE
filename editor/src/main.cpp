#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Camera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <chrono>

int main(int argc, char **argv)
{
    PlutoGE::core::Engine engine;

    if (!engine.Initialize())
    {
        return -1;
    }

    auto &window = engine.GetWindow();
    const auto &input = window.GetInputState();
    auto &renderer = engine.GetRenderer();

    auto material = engine.GetAssetManager().CreateDefaultMaterial();
    auto texturedMaterial = engine.GetAssetManager().CreateDefaultMaterial();
    texturedMaterial->SetAlbedoTexture(engine.GetAssetManager().LoadTexture("U:\\Documents\\cobble\\cobble_base.png"));
    texturedMaterial->SetNormalTexture(engine.GetAssetManager().LoadTexture("U:\\Documents\\cobble\\cobble_normal.png"));
    texturedMaterial->SetColor(glm::vec3(1.0f, 1.0f, 1.0f)); // Set a base color for the material

    auto mesh = PlutoGE::render::Mesh::Cube();
    auto camera = PlutoGE::render::Camera(PlutoGE::render::CameraConfig{
        .fovY = 90.0f,
        .aspectRatio = 800.0f / 600.0f,
        .nearPlane = 0.1f,
        .farPlane = 100.0f,
    });
    glm::vec3 cameraPos(0.0f, 0.0f, 5.0f);

    renderer.SetCamera(&camera);

    PlutoGE::render::RenderCommand command;
    command.model = glm::mat4(1.0f);     // Identity model matrix
    command.material = texturedMaterial; // material; // Set this to a valid material
    command.mesh = mesh;                 // Set this to a valid mesh

    auto modelMatrix = glm::mat4(1.0f); // Identity model matrix

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastTime = startTime;
    auto deltaTime = std::chrono::duration<float>(0.0f);

    glm::vec3 direction = glm::vec3(0.0f, 0.0f, 0.0f);
    while (!window.ShouldClose())
    {
        camera.SetViewMatrix(glm::lookAt(
            cameraPos,                                // Camera position
            cameraPos + glm::vec3(0.0f, 0.0f, -1.0f), // Target (looking forward)
            glm::vec3(0.0f, 1.0f, 0.0f)               // Up
            ));

        renderer.SubmitRenderCommand(command);

        renderer.BeginFrame();

        renderer.RenderFrame();

        renderer.EndFrame();

        window.PollEvents();
    }

    return 0;
}