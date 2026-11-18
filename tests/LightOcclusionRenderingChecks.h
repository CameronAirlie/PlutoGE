#pragma once
#include "GlobalIlluminationRenderingChecks.h"
#include "PlutoGE/render/DdgiPacket.h"

#include <cstring>
#include <iostream>
#include <string>

// Traced shadows (the light visibility mask's world occlusion, docs/LIGHT_OCCLUSION.md): a point light without a shadow
// map stands on one side of a wall. Without traced shadows its light passes through the wall onto the floor behind it;
// with them that floor must go dark, while open floor in front of the wall (which nothing occludes) keeps its light.
// The world representation is the scene DDGI keeps: `source` picks it (hardware rays or voxels). Returns false when
// the device cannot run that source.
inline bool CheckTracedLightShadows(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                                    PlutoGE::render::DdgiRaySource source)
{
    using namespace GlobalIlluminationChecks;
    const bool hardware = source == DdgiRaySource::Hardware;
    const std::string name = hardware ? "Hardware" : "Voxels";
    Require(renderer.Resize(96, 64), "Traced shadow viewport resize failed");
    auto floorMesh = MakeQuad(renderer, {0, 0, 0}, {6, 0, 0}, {0, 0, -6});
    auto cube = MakeCube(renderer);
    BasicDraw floor{.mesh = &floorMesh};
    floor.preparationRevision = 0x9001;
    // A 0.3-thick wall across the floor at x = 0, taller than the light.
    auto wall = Box(cube, {-0.15f, 0.0f, -6.0f}, {0.15f, 3.0f, 6.0f});
    wall.preparationRevision = 0x9002;
    const std::array draws{floor, wall};

    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = true;
    scene.lighting.directionalIntensity = 0.0f;
    scene.lighting.ambientIntensity = 0.02f;
    BasicPointLight lamp{.position = {1.5f, 1.0f, 0.0f}, .range = 12.0f, .color = glm::vec3(1.0f), .intensity = 4.0f};
    lamp.castsShadows = false;
    scene.lighting.pointLights = {lamp};

    DdgiSettings settings;
    settings.raySource = source;
    // DDGI only supplies the scene here: its own indirect light (hits are lit by local lights unshadowed) would add
    // the same lamp light behind the wall with and without traced shadows and blur the direct-light measurement.
    settings.intensity = 0.0f;
    settings.grid.counts = {16, 6, 16};
    settings.grid.spacing = 0.75f;
    settings.grid.probesPerFrame = DdgiGridSettings::MaxProbesPerFrame;
    settings.voxels = {.resolution = 64, .cascades = 2, .volumeSize = 24.0f, .updateSpeed = 4.0f,
                       .injectLocalLights = false, .commandBudget = 256};
    const auto ddgi = EncodeDdgiPacket(settings, &settings);
    const auto effects = std::span(&ddgi, 1);
    // DDGI keeps the scene a frame's traced shadows test, from the frame before; voxels take a few frames to publish.
    constexpr int Frames = 24;

    const auto meanLuminance = [](const std::vector<float> &image)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < image.size(); i += 4)
            sum += 0.2126 * image[i] + 0.7152 * image[i + 1] + 0.0722 * image[i + 2];
        return sum / double(image.size() / 4);
    };
    const auto render = [&](std::uint32_t tracedLights, bool lightTraced)
    {
        scene.lighting.tracedShadowLights = tracedLights;
        scene.lighting.pointLights[0].tracedShadows = lightTraced;
        return scene.Render(draws, effects, Frames);
    };

    // Behind the wall: the floor at x < 0, which the lamp can only reach through it.
    scene.Look({-3.0f, 2.2f, 2.5f}, {-1.8f, 0.0f, 0.0f});
    const auto leaking = render(0, true);
    const bool hardwareRan = renderer.GetFrameStats().ddgiHardwareRays;
    if (hardware && !hardwareRan)
    {
        std::cout << "Traced shadows (" << name << ") skipped: no hardware ray tracing\n";
        return false;
    }
    const auto occluded = render(8, true);
    const auto &stats = renderer.GetFrameStats();
    if (stats.tracedShadowLights != 1 || std::strcmp(stats.lightOcclusionSource, name.c_str()) != 0)
        std::cerr << "Traced shadows (" << name << "): " << stats.tracedShadowLights << " traced lights, source '"
                  << stats.lightOcclusionSource << "'\n";
    Require(stats.tracedShadowLights == 1, "The lamp was not given a traced shadow");
    Require(std::strcmp(stats.lightOcclusionSource, name.c_str()) == 0, "Traced shadows used another world representation");
    const auto optedOut = render(8, false);
    Require(renderer.GetFrameStats().tracedShadowLights == 0, "A light with traced shadows off was traced");
    const double leak = meanLuminance(leaking), behind = meanLuminance(occluded), behindOptedOut = meanLuminance(optedOut);

    // In front of the wall: open floor between the lamp and the camera, which nothing occludes.
    scene.Look({3.5f, 2.2f, 2.5f}, {2.0f, 0.0f, 0.0f});
    const double open = meanLuminance(render(0, true)), openTraced = meanLuminance(render(8, true));

    const double behindRatio = behind / leak, optedOutRatio = behindOptedOut / leak, openRatio = openTraced / open;
    std::cout << "Traced shadows (" << name << "): floor behind the wall " << behindRatio << " of its leaked light, opted out "
              << optedOutRatio << ", open floor " << openRatio << '\n';
    Require(behindRatio < 0.35, "Traced shadows did not stop a light at a wall");
    Require(optedOutRatio > 0.95, "A light with traced shadows off lost its light");
    Require(openRatio > 0.95 && openRatio < 1.05, "Traced shadows darkened open floor (self-shadowing)");
    scene.lighting.pointLights.clear();
    return true;
}
