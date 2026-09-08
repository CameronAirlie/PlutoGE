#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

template <class ReadPixels>
void CheckParticlePointRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-.9f,-.9f,0}}, {{0,0,1}}, {{0,0}}},
        {{{ .9f,-.9f,0}}, {{0,0,1}}, {{1,0}}},
        {{{ .9f, .9f,0}}, {{0,0,1}}, {{1,1}}},
        {{{-.9f, .9f,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices = {0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw receiver;
    receiver.mesh = &mesh;
    receiver.model[3].z = .2f;
    BasicLighting lighting;
    lighting.ambientIntensity = 0;
    lighting.directionalIntensity = 0;
    lighting.cameraPosition = {0, 0, 2};
    const auto center = [&]() {
        const auto pixels = readPixels(renderer.GetColorTexture());
        const auto index = (renderer.GetHeight() / 2 * renderer.GetWidth() + renderer.GetWidth() / 2) * 4;
        require(pixels.size() > index + 2, "Particle/point readback failed");
        return std::array<int, 3>{int(pixels[index]), int(pixels[index + 1]), int(pixels[index + 2])};
    };
    renderer.Render(glm::mat4(1), lighting, {&receiver, 1});
    const auto dark = center();
    lighting.pointLights.push_back({{0, 0, 1.2f}, 4, {1, 0, 0}, 4, false});
    renderer.Render(glm::mat4(1), lighting, {&receiver, 1});
    const auto lit = center();
    require(lit[0] > dark[0] + 40 && lit[1] < 10, "Point light did not illuminate with its color");
    BasicDraw blocker = receiver;
    blocker.model[3].z = .7f;
    lighting.pointLights[0].castsShadows = true;
    renderer.Render(glm::mat4(1), lighting, {&receiver, 1}, {}, {&blocker, 1});
    const auto shadow = center();
    require(shadow[0] + 30 < lit[0], "Point light shadow did not occlude receiver");
    lighting.pointLights[0].castsShadows = false;
    renderer.Render(glm::mat4(1), lighting, {&receiver, 1}, {}, {&blocker, 1});
    require(center()[0] > shadow[0] + 30, "Disabling point shadows left stale occlusion");
    // Rotate the complete setup to exercise every cube face and atlas orientation.
    const std::array rotations{
        glm::mat4(1), glm::rotate(glm::mat4(1), glm::radians(180.0f), glm::vec3(0,1,0)),
        glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(0,1,0)),
        glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(0,1,0)),
        glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1,0,0)),
        glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(1,0,0))};
    for (const auto &rotation : rotations)
    {
        auto rotatedReceiver=receiver; rotatedReceiver.model=rotation*receiver.model;
        auto rotatedBlocker=blocker; rotatedBlocker.model=rotation*blocker.model;
        lighting.cameraPosition=glm::vec3(rotation*glm::vec4(0,0,2,1));
        lighting.pointLights[0].position=glm::vec3(rotation*glm::vec4(0,0,1.2f,1));
        lighting.pointLights[0].castsShadows=true;
        renderer.Render(glm::inverse(rotation),lighting,{&rotatedReceiver,1},{},{&rotatedBlocker,1});
        require(center()[0]+30<lit[0],"Point shadow cube face failed");
        rotatedBlocker.alphaMode=1; rotatedBlocker.baseColor.a=0;
        renderer.Render(glm::inverse(rotation),lighting,{&rotatedReceiver,1},{},{&rotatedBlocker,1});
        require(center()[0]>shadow[0]+30,"Transparent masked caster blocked a point light");
    }
    lighting.cameraPosition={0,0,2}; lighting.pointLights[0].position={0,0,1.2f};
    auto instances=std::make_shared<std::vector<glm::mat4>>(); instances->push_back(blocker.model);
    blocker.instanceModels=instances;
    renderer.Render(glm::mat4(1),lighting,{&receiver,1},{},{&blocker,1});
    require(center()[0]+30<lit[0],"Instanced caster did not cast a point shadow");
    lighting.pointLights[0].castsShadows=false;
    lighting.pointLights[0].range = .2f;
    renderer.Render(glm::mat4(1), lighting, {&receiver, 1});
    require(center()[0] < 10, "Point light ignored its range");
    lighting.pointLights.clear();
    BasicParticleDraw particle;
    particle.parameters.values[0] = glm::vec4(1);
    particle.parameters.values[2].x = 1;
    particle.parameters.values[3] = {1, 1, 0, 0};
    for (auto index : indices)
    {
        const auto &vertex = vertices[index];
        particle.vertices.push_back({{vertex.position[0], vertex.position[1], .6f},
                                     {0, 1, 0, .5f},
                                     {vertex.uv[0], vertex.uv[1]},
                                     {0, 1, 0, 1},
                                     {0, 0, .6f}});
    }
    renderer.Render(glm::mat4(1), lighting, {&receiver, 1}, {}, {}, PostProcessDebugView::None, nullptr, nullptr, true,
                    {}, {&particle, 1});
    const auto blended = center();
    require(blended[1] > 110 && blended[1] < 145 && blended[0] < 10, "Particle did not blend with half opacity");
    // Use a monotonic reverse-depth reconstruction for this synthetic identity camera.
    // OpenGL without clip control maps the input depth from NO to [0,1]; Vulkan uses ZO.
    particle.parameters.inverseProjection[2][2] = -1;
    particle.parameters.inverseProjection[3][2] = 1;
    particle.parameters.values[2].z=1; particle.parameters.values[2].w=1;
    renderer.Render(glm::mat4(1),lighting,{&receiver,1},{},{},PostProcessDebugView::None,nullptr,nullptr,true,{}, {&particle,1});
    require(center()[1]>40 && center()[1]<110,("Soft particle depth fade did not reduce opacity: green="+std::to_string(center()[1])).c_str());
    particle.parameters.values[2].z=0;
    for (auto &vertex : particle.vertices)
        vertex.position.z = .1f;
    renderer.Render(glm::mat4(1), lighting, {&receiver, 1}, {}, {}, PostProcessDebugView::None, nullptr, nullptr, true,
                    {}, {&particle, 1});
    require(center()[1] < 10, "Particle ignored opaque depth");
}
