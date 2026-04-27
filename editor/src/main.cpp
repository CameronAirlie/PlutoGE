#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

int main(int argc, char **argv)
{
    PlutoGE::core::Engine engine;

    if (!engine.Initialize())
    {
        return -1;
    }

    auto &window = engine.GetWindow();
    auto &renderer = engine.GetRenderer();

    auto material = engine.GetAssetManager().CreateDefaultMaterial();
    auto texturedMaterial = engine.GetAssetManager().CreateDefaultMaterial();
    texturedMaterial->SetAlbedoTexture(engine.GetAssetManager().LoadTexture("U:\\Documents\\cobble\\cobble_base.png"));
    // texturedMaterial->SetNormalTexture(engine.GetAssetManager().LoadTexture("U:\\Documents\\cobble\\cobble_normal.png"));
    texturedMaterial->SetColor(glm::vec3(1.0f, 1.0f, 1.0f)); // Set a base color for the material

    auto mesh = PlutoGE::render::Mesh::Cube();

    PlutoGE::render::RenderCommand command;
    command.material = texturedMaterial; // material; // Set this to a valid material
    command.mesh = mesh;                 // Set this to a valid mesh

    auto modelMatrix = glm::mat4(1.0f); // Identity model matrix

    while (!window.ShouldClose())
    {

        float time = static_cast<float>(glfwGetTime());

        // Move the cube closer and farther over time for demonstration
        float distance = 5.0f + sin(time * 1.5f) * 8.0f;
        modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -distance));

        // Rotate the model matrix over time for demonstration
        modelMatrix = glm::rotate(modelMatrix, time * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));

        command.model = modelMatrix; // Set the model matrix

        renderer.SubmitRenderCommand(command);

        renderer.BeginFrame();

        renderer.RenderFrame();

        renderer.EndFrame();

        window.PollEvents();
    }

    return 0;
}