#pragma once
#include "PlutoGE/core/FrameWorker.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/ShaderGraph.h"
#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// RhiRenderService's render thread: frames prepared on the game thread and recorded on another thread must equal frames
// rendered on one thread, and device updates and destruction requested by other threads while a render thread records
// must wait for the next frame, leaving what the recording frame uses intact.
template <class Device>
void CheckRenderThreadRecording(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE;
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    SetShaderGraphTimeSeconds(1.0f);

    MeshConfig quadConfig;
    quadConfig.data.vertices = {{{-1, 0, -1}, {0, 1, 0}, {0, 0}, {1, 1, 1, 1}}, {{1, 0, -1}, {0, 1, 0}, {1, 0}, {1, 1, 1, 1}},
                                {{1, 0, 1}, {0, 1, 0}, {1, 1}, {1, 1, 1, 1}}, {{-1, 0, 1}, {0, 1, 0}, {0, 1}, {1, 1, 1, 1}}};
    quadConfig.data.indices = {0, 2, 1, 0, 3, 2};
    MeshConfig triangleConfig;
    triangleConfig.data.vertices = {{{-.5f, 0, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}},
                                    {{.5f, 0, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}},
                                    {{0, 1, 0}, {0, 0, 1}, {.5f, 1}, {1, 1, 1, 1}}};
    triangleConfig.data.indices = {0, 1, 2, 0, 2, 1};
    Mesh quad(quadConfig), triangle(triangleConfig);
    Material ground({.color = {.7f, .7f, .65f, 1}}), red({.color = {.9f, .2f, .2f, 1}}), blue({.color = {.2f, .3f, .9f, 1}});

    BasicLighting lighting;
    lighting.directionalDirection = glm::normalize(glm::vec3(-.4f, -1, -.3f));
    lighting.directionalIntensity = 3;
    lighting.ambientIntensity = .2f;
    lighting.shadowsEnabled = true;
    CameraData camera{.view = glm::lookAt(glm::vec3(0, 3, 6), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)),
                      .projection = glm::perspective(glm::radians(55.0f), 96.0f / 72.0f, 0.1f, 50.0f)};
    lighting.cameraPosition = {0, 3, 6};
    lighting.view = camera.view;

    // Five frames of a scene with shadows whose casters move, so the frames depend on the previous ones.
    const auto commandsFor = [&](int frame)
    {
        std::vector<RenderCommand> commands;
        RenderCommand floor{.material = &ground, .mesh = &quad};
        floor.model = glm::scale(glm::mat4(1), glm::vec3(4));
        commands.push_back(floor);
        for (int index = 0; index < 6; ++index)
        {
            RenderCommand caster{.material = index % 2 ? &red : &blue, .mesh = &triangle};
            const auto position = glm::vec3(float(index) - 2.5f, .1f, float(index % 3) - 1 + .15f * float(frame));
            caster.model = glm::rotate(glm::translate(glm::mat4(1), position), .4f * float(index + frame), glm::vec3(0, 1, 0));
            caster.previousModel = caster.model;
            caster.worldBounds = {position, 1.0f};
            commands.push_back(caster);
        }
        for (auto &command : commands)
            if (command.worldBounds.radius == 0) command.worldBounds = {glm::vec3(command.model[3]), 6.0f};
        return commands;
    };
    const auto renderFrames = [&](bool threaded)
    {
        RhiSceneRenderer renderer;
        require(renderer.Initialize(device, shaders), "Render thread check: renderer initialization failed");
        core::FrameWorker renderThread("Render thread check");
        for (int frame = 0; frame < 5; ++frame)
        {
            const auto commands = commandsFor(frame);
            if (!threaded)
            {
                require(renderer.Render(96, 72, camera, lighting, commands, commands), "Render thread check: render failed");
                continue;
            }
            auto prepared = renderer.Prepare(96, 72, camera, lighting, commands, commands);
            require(prepared.has_value(), "Render thread check: preparation failed");
            renderThread.Submit([&]
            {
                device.SetRecordingThread(std::this_thread::get_id());
                renderer.Record(*prepared);
                device.SetRecordingThread({});
            });
            renderThread.Wait();
        }
        return device.ReadTextureRgba8(renderer.GetColorTexture());
    };
    const auto serial = renderFrames(false);
    bool varied = false;
    for (std::size_t index = 4; index < serial.size() && !varied; index += 4)
        varied = !std::equal(serial.begin(), serial.begin() + 4, serial.begin() + static_cast<std::ptrdiff_t>(index));
    require(varied, "Render thread check: the scene rendered as a flat image");
    require(renderFrames(true) == serial, "Frames recorded on a render thread differ from frames rendered on one thread");

    // Device calls from another thread while a render thread records wait for the next frame.
    const auto pixel = [](unsigned char r, unsigned char g, unsigned char b)
    {
        return std::vector<std::byte>{std::byte{r}, std::byte{g}, std::byte{b}, std::byte{255}};
    };
    const auto green = pixel(0, 255, 0), blueTexel = pixel(0, 0, 255), redTexel = pixel(255, 0, 0);
    rhi::Texture updated(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled,
                                                       "Deferred update", true, 1, false, 1}, green));
    const auto doomed = device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled,
                                              "Deferred destruction", true, 1, false, 1}, blueTexel);
    std::atomic<bool> recording{false}, release{false};
    std::vector<std::byte> doomedDuringFrame;
    {
        core::FrameWorker renderThread("Deferred device calls");
        renderThread.Submit([&]
        {
            device.SetRecordingThread(std::this_thread::get_id());
            device.GetImmediateContext().BeginFrame("Deferred device calls");
            recording = true;
            while (!release) std::this_thread::yield();
            doomedDuringFrame = device.ReadTextureRgba8(doomed);
            device.GetImmediateContext().Submit();
            device.SetRecordingThread({});
        });
        while (!recording) std::this_thread::yield();
        require(device.UpdateTexture(updated.Get(), {0, 0, 1, 1}, redTexel), "Deferred texture update was rejected");
        device.DestroyTexture(doomed);
        release = true;
        renderThread.Wait();
    }
    require(doomedDuringFrame == blueTexel, "A texture destroyed by another thread vanished while the render thread recorded");
    require(device.ReadTextureRgba8(updated.Get()) == green, "Another thread's texture update applied during the recording frame");
    device.GetImmediateContext().BeginFrame("Apply deferred device calls");
    device.GetImmediateContext().Submit();
    require(device.ReadTextureRgba8(updated.Get()) == redTexel, "The next frame did not apply the deferred texture update");
    bool destroyed = false;
    try { (void)device.ReadTextureRgba8(doomed); }
    catch (const std::invalid_argument &) { destroyed = true; }
    require(destroyed, "The next frame did not apply the deferred destruction");
    std::cout << "Render thread recording matches single-threaded rendering; cross-thread device calls are deferred" << std::endl;
}
