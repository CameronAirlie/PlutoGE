#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/DdgiConfig.h"
#include "PlutoGE/render/DdgiPacket.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Screen-space GI and DDGI on the real multipass paths of either backend. Both replace part of the ambient light the
// geometry pass wrote, so besides showing bounce light they must leave lighting unchanged where nothing occludes it.
namespace GlobalIlluminationChecks
{
    using namespace PlutoGE::render;

    inline void Require(bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    }

    // A quad around `center` spanned by the half-axes u and v, facing cross(u, v).
    inline BasicMesh MakeQuad(BasicRenderer &renderer, glm::vec3 center, glm::vec3 u, glm::vec3 v)
    {
        const glm::vec3 normal = glm::normalize(glm::cross(u, v));
        const std::array<glm::vec3, 4> corners{center - u - v, center + u - v, center + u + v, center - u + v};
        constexpr std::array<std::array<float, 2>, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        std::array<BasicVertex, 4> vertices{};
        for (std::size_t index = 0; index < vertices.size(); ++index)
        {
            vertices[index].position = {corners[index].x, corners[index].y, corners[index].z};
            vertices[index].normal = {normal.x, normal.y, normal.z};
            vertices[index].uv = uvs[index];
        }
        constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        return renderer.CreateMesh({vertices, indices});
    }

    struct Scene
    {
        BasicRenderer &renderer;
        rhi::IRenderDevice &device;
        BasicLighting lighting;
        glm::mat4 projection{1.0f};

        void Look(glm::vec3 eye, glm::vec3 target)
        {
            lighting.cameraPosition = eye;
            lighting.view = glm::lookAtRH(eye, target, glm::vec3(0, 1, 0));
            constexpr float nearPlane = 0.1f, farPlane = 100.0f;
            const float focal = 1.0f / std::tan(glm::radians(55.0f) * 0.5f);
            projection = glm::mat4(0.0f);
            projection[0][0] = focal * float(renderer.GetHeight()) / float(renderer.GetWidth());
            projection[1][1] = focal;
            projection[2][3] = -1;
            const bool zeroToOne = device.UsesZeroToOneClipDepth();
            projection[2][2] = zeroToOne ? nearPlane / (farPlane - nearPlane) : (farPlane + nearPlane) / (farPlane - nearPlane);
            projection[3][2] = zeroToOne ? farPlane * nearPlane / (farPlane - nearPlane)
                                         : 2 * farPlane * nearPlane / (farPlane - nearPlane);
        }

        // Linear HDR scene colour after `frames` frames of the effects (GI before tone mapping).
        std::vector<float> Render(std::span<const BasicDraw> draws, std::span<const BasicPostProcessEffect> effects,
                                  int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
                renderer.Render(projection * lighting.view, lighting, draws, effects, {}, PostProcessDebugView::None,
                                nullptr, nullptr, true, draws, {}, {}, true);
            auto image = device.ReadTextureRgbaFloat(renderer.GetColorTexture());
            for (const float value : image)
                Require(std::isfinite(value) && value >= 0.0f, "GI produced a negative or non-finite colour");
            return image;
        }
    };

    // Pixels that gained red light without comparable green: bounce light from the red emitter.
    inline std::size_t RedBleed(const std::vector<float> &base, const std::vector<float> &lit)
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < base.size(); i += 4)
        {
            const float red = lit[i] - base[i], green = lit[i + 1] - base[i + 1];
            count += red > 0.005f && red > 4.0f * std::max(green, 0.0f);
        }
        return count;
    }

    // Mean luminance of the pixels the baseline lit (the visible geometry).
    inline std::array<double, 2> LitMeans(const std::vector<float> &base, const std::vector<float> &lit)
    {
        double baseSum = 0.0, litSum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < base.size(); i += 4)
        {
            const double baseLuminance = 0.2126 * base[i] + 0.7152 * base[i + 1] + 0.0722 * base[i + 2];
            if (baseLuminance < 0.05) continue;
            baseSum += baseLuminance;
            litSum += 0.2126 * lit[i] + 0.7152 * lit[i + 1] + 0.0722 * lit[i + 2];
            ++count;
        }
        Require(count > 50, "GI check sees too little geometry");
        return {baseSum / double(count), litSum / double(count)};
    }
}

inline void CheckSsgiRendering(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device)
{
    using namespace GlobalIlluminationChecks;
    Require(renderer.Resize(96, 64), "SSGI viewport resize failed");
    auto floorMesh = MakeQuad(renderer, {0, 0, 0}, {4, 0, 0}, {0, 0, -4});
    auto wallMesh = MakeQuad(renderer, {0, 1.5f, -1}, {4, 0, 0}, {0, 1.5f, 0});
    BasicDraw floor{.mesh = &floorMesh};
    BasicDraw wall{.mesh = &wallMesh};
    wall.emission = {2.0f, 0.0f, 0.0f};
    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = false;
    scene.lighting.directionalIntensity = 0;
    scene.lighting.ambientIntensity = 0;
    scene.Look({0, 2.2f, 5}, {0, 0.4f, 0});

    BasicPostProcessEffect ssgi{BasicPostProcessEffectType::SSGI};
    ssgi.quality = 24;
    ssgi.parameters[0] = {1.0f, 4.0f, 0.6f, 0.85f}; // intensity, ray distance, thickness, history weight
    ssgi.parameters[1] = {2.0f, 2.0f, 0.0f, 1.0f};  // rays, denoise passes, debug view, half resolution
    ssgi.parameters[2] = {0.05f, 0.85f, 0.0f, 0.0f};
    const std::array bounceScene{floor, wall};
    const auto base = scene.Render(bounceScene, {}, 2);
    const auto lit = scene.Render(bounceScene, std::span(&ssgi, 1), 24);
    const auto bled = RedBleed(base, lit);
    if (bled <= 50) std::cerr << "SSGI red bleed pixels: " << bled << '\n';
    Require(bled > 50, "SSGI did not bounce the emissive wall onto the floor");

    // Under uniform ambient light an open floor occludes nothing: every ray misses and the image must not change.
    scene.lighting.ambientIntensity = 1.0f;
    const std::array openScene{floor};
    const auto ambientBase = scene.Render(openScene, {}, 2);
    const auto ambientLit = scene.Render(openScene, std::span(&ssgi, 1), 8);
    float largest = 0.0f;
    for (std::size_t i = 0; i < ambientBase.size(); ++i)
        largest = std::max(largest, std::abs(ambientBase[i] - ambientLit[i]));
    if (largest >= 0.002f) std::cerr << "SSGI open floor difference: " << largest << '\n';
    Require(largest < 0.002f, "SSGI changed unoccluded ambient lighting");

    for (const float view : {1.0f, 2.0f})
    {
        ssgi.parameters[1].z = view;
        (void)scene.Render(bounceScene, std::span(&ssgi, 1), 2);
    }
    std::cout << "SSGI bounce light, unoccluded ambient preservation and debug views passed (" << bled
              << " bounce pixels)\n";
}

inline void CheckDdgiRendering(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device)
{
    using namespace GlobalIlluminationChecks;
    Require(renderer.Resize(96, 64), "DDGI viewport resize failed");
    auto floorMesh = MakeQuad(renderer, {0, 0, 0}, {6, 0, 0}, {0, 0, -6});
    auto wallMesh = MakeQuad(renderer, {0, 1.5f, -1}, {4, 0, 0}, {0, 1.5f, 0});
    auto roofMesh = MakeQuad(renderer, {0, 1.0f, -1.25f}, {4, 0, 0}, {0, 0, 2.75f});
    BasicDraw floor{.mesh = &floorMesh};
    BasicDraw wall{.mesh = &wallMesh};
    wall.emission = {2.0f, 0.0f, 0.0f};
    BasicDraw roof{.mesh = &roofMesh};
    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = false;
    scene.lighting.directionalIntensity = 0;

    DdgiSettings settings;
    settings.grid.counts = {16, 6, 24};
    settings.grid.spacing = 0.75f;
    settings.grid.blendedRays = 64;
    // Enough for the finest cascade to refresh every probe each frame (CascadeBudget splits it 4:2:1).
    settings.grid.probesPerFrame = DdgiGridSettings::MaxProbesPerFrame;
    settings.hysteresis = 0.5f;
    settings.voxels = {.resolution = 64, .cascades = 2, .volumeSize = 24.0f, .updateSpeed = 4.0f,
                       .injectLocalLights = false, .commandBudget = 256};
    auto ddgi = EncodeDdgiPacket(settings, &settings);
    constexpr int ConvergenceFrames = 48;

    // Bounce light from an emitter, with no other light in the scene.
    scene.lighting.ambientIntensity = 0;
    scene.Look({0, 1.0f, 3.5f}, {0, 0.3f, 0});
    const std::array bounceScene{floor, wall};
    const auto base = scene.Render(bounceScene, {}, 2);
    const auto lit = scene.Render(bounceScene, std::span(&ddgi, 1), ConvergenceFrames);
    const auto stats = renderer.GetFrameStats();
    Require(stats.ddgiProbesTraced > 0 && stats.ddgiFramesPerSweep == 1, "DDGI traced no probes");
    const auto bled = RedBleed(base, lit);
    if (bled <= 50) std::cerr << "DDGI red bleed pixels: " << bled << '\n';
    Require(bled > 50, "DDGI did not bounce the emissive wall onto the floor");

    // The same scene either side of the world origin, refreshing a sixth of the probes per frame. Probe storage wraps
    // negative cells; every surface must still find its own probes (whole refreshes would hide a wrong lookup).
    {
        auto partial = settings;
        partial.grid.probesPerFrame = settings.grid.ProbeCount() / 6;
        const auto packet = EncodeDdgiPacket(partial, &partial);
        const auto redGain = [&](glm::vec3 offset)
        {
            auto movedFloor = floor, movedWall = wall;
            movedFloor.model = movedWall.model = glm::translate(glm::mat4(1.0f), offset);
            scene.Look(offset + glm::vec3(0, 1.0f, 3.5f), offset + glm::vec3(0, 0.3f, 0));
            const std::array moved{movedFloor, movedWall};
            const auto before = scene.Render(moved, {}, 2);
            const auto after = scene.Render(moved, std::span(&packet, 1), 3 * ConvergenceFrames);
            double gain = 0.0;
            for (std::size_t i = 0; i < before.size(); i += 4) gain += after[i] - before[i];
            return gain;
        };
        // Whole multiples of the probe spacing and voxel size, so both placements align identically.
        const double positive = redGain({36, 0, 36}), straddling = redGain({0, 0, 0});
        if (!(positive > 0.0) || std::abs(straddling - positive) > 0.15 * positive)
            std::cerr << "DDGI red gain at +36: " << positive << ", across the origin: " << straddling << '\n';
        Require(positive > 0.0 && std::abs(straddling - positive) <= 0.15 * positive,
                "DDGI lit the same scene differently across the world origin");
    }

    // Unoccluded flat ambient: the probes see only the environment, which they must reproduce (the geometry pass
    // omits ambient Fresnel, so allow a few percent).
    scene.lighting.ambientIntensity = 1.0f;
    scene.Look({0, 0.5f, 4.5f}, {0, 0.3f, 0});
    const std::array openScene{floor};
    const auto openMeans = LitMeans(scene.Render(openScene, {}, 2),
                                    scene.Render(openScene, std::span(&ddgi, 1), ConvergenceFrames));
    const double openRatio = openMeans[1] / openMeans[0];
    if (openRatio < 0.85 || openRatio > 1.05) std::cerr << "DDGI open floor ratio: " << openRatio << '\n';
    Require(openRatio > 0.85 && openRatio < 1.05, "DDGI changed the brightness of unoccluded ambient lighting");

    // A roof blocks most of the sky above the floor beneath it, which flat ambient light cannot show. Grey surfaces
    // keep the light that multiple bounces return under the roof realistic.
    auto greyFloor = floor;
    greyFloor.baseColor = roof.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
    const std::array roofScene{greyFloor, roof};
    const auto roofMeans = LitMeans(scene.Render(roofScene, {}, 2),
                                    scene.Render(roofScene, std::span(&ddgi, 1), ConvergenceFrames));
    const double roofRatio = roofMeans[1] / roofMeans[0];
    if (roofRatio >= 0.9) std::cerr << "DDGI roofed floor ratio: " << roofRatio << '\n';
    Require(roofRatio < 0.9, "DDGI did not occlude ambient light under a roof");

    // Coarser cascades extend the volume: a roof beyond the finest cascade (11 units across here) still shades the
    // floor beneath it, which a single cascade leaves to flat ambient light.
    {
        auto bigFloorMesh = MakeQuad(renderer, {0, 0, -10}, {20, 0, 0}, {0, 0, -20});
        auto farRoofMesh = MakeQuad(renderer, {0, 1.0f, -22}, {10, 0, 0}, {0, 0, 8});
        BasicDraw bigFloor{.mesh = &bigFloorMesh}, farRoof{.mesh = &farRoofMesh};
        bigFloor.baseColor = farRoof.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
        const std::array farScene{bigFloor, farRoof};
        scene.Look({0, 1.5f, 4.5f}, {0, 0.5f, -20});
        const auto farBase = scene.Render(farScene, {}, 2);
        const auto farMean = [&](int cascades)
        {
            auto cascaded = settings;
            cascaded.grid.cascades = cascades;
            const auto packet = EncodeDdgiPacket(cascaded, &cascaded);
            return LitMeans(farBase, scene.Render(farScene, std::span(&packet, 1), ConvergenceFrames))[1];
        };
        const double single = farMean(1), cascaded = farMean(3);
        if (cascaded >= 0.95 * single) std::cerr << "DDGI far roof, 1 cascade: " << single << ", 3: " << cascaded << '\n';
        Require(cascaded < 0.95 * single, "DDGI cascades did not extend the volume beyond the finest cascade");
    }

    for (const int view : {PLUTO_DDGI_VIEW_IRRADIANCE, PLUTO_DDGI_VIEW_INDIRECT, PLUTO_DDGI_VIEW_PROBES})
    {
        settings.debugView = view;
        ddgi = EncodeDdgiPacket(settings, &settings);
        (void)scene.Render(roofScene, std::span(&ddgi, 1), 2);
    }
    std::cout << "DDGI bounce light (" << bled << " pixels), open ambient ratio " << openRatio
              << ", roofed ambient ratio " << roofRatio << " and debug views passed\n";
}

// Steady-state GPU timings of SSGI and of DDGI at each quality level, in a lit room with an emissive panel. Stage
// scopes are reported as recorded; DDGI's includes its share of the voxel scene's upkeep.
inline void CheckGlobalIlluminationPerformance(PlutoGE::render::BasicRenderer &renderer,
                                               PlutoGE::render::rhi::IRenderDevice &device,
                                               PlutoGE::render::rhi::Extent2D extent)
{
    using namespace GlobalIlluminationChecks;
    Require(renderer.Resize(extent.width, extent.height), "GI benchmark resize failed");
    auto floorMesh = MakeQuad(renderer, {0, 0, 0}, {8, 0, 0}, {0, 0, -8});
    auto backMesh = MakeQuad(renderer, {0, 3, -8}, {8, 0, 0}, {0, 3, 0});
    auto leftMesh = MakeQuad(renderer, {-8, 3, 0}, {0, 0, -8}, {0, 3, 0});
    auto panelMesh = MakeQuad(renderer, {4, 1.5f, -7.9f}, {1.5f, 0, 0}, {0, 1, 0});
    BasicDraw floor{.mesh = &floorMesh}, back{.mesh = &backMesh}, left{.mesh = &leftMesh}, panel{.mesh = &panelMesh};
    floor.baseColor = {0.7f, 0.7f, 0.7f, 1.0f};
    back.baseColor = {0.8f, 0.3f, 0.2f, 1.0f};
    panel.emission = {4.0f, 3.0f, 2.0f};
    const std::array draws{floor, back, left, panel};
    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = false;
    scene.lighting.ambientIntensity = 0.3f;
    scene.lighting.directionalIntensity = 2.0f;
    scene.lighting.directionalDirection = glm::normalize(glm::vec3(0.4f, -1.0f, -0.3f));
    scene.Look({3, 2, 6}, {-1, 1, -4});

    const auto measure = [&](const char *label, const BasicPostProcessEffect &effect, const char *prefix,
                             const char *total)
    {
        double totalMs = 0.0;
        std::vector<std::pair<std::string, double>> stages;
        int samples = 0;
        for (int frame = 0; frame < 96 + 192; ++frame)
        {
            renderer.Render(scene.projection * scene.lighting.view, scene.lighting, draws, std::span(&effect, 1), {},
                            PostProcessDebugView::None, nullptr, nullptr, true, draws);
            const auto timing = device.GetTimingStats("Scene");
            if (frame < 96 || !timing.hasGpuResult) continue;
            for (const auto &gpuScope : timing.gpuScopes)
            {
                if (gpuScope.name == total) totalMs += gpuScope.milliseconds;
                if (!gpuScope.name.starts_with(prefix) && gpuScope.name != "RHI Voxel Scene") continue;
                const auto stage = std::ranges::find(stages, gpuScope.name, &std::pair<std::string, double>::first);
                if (stage == stages.end()) stages.emplace_back(gpuScope.name, gpuScope.milliseconds);
                else stage->second += gpuScope.milliseconds;
            }
            ++samples;
        }
        Require(samples > 0, "GI benchmark has no GPU timings");
        std::cout << label << " (" << extent.width << " x " << extent.height << "): " << totalMs / samples << " ms";
        for (const auto &[name, milliseconds] : stages) std::cout << ", " << milliseconds / samples << " ms " << name.substr(4);
        std::cout << " (" << samples << " samples)\n";
    };

    BasicPostProcessEffect ssgi{BasicPostProcessEffectType::SSGI};
    ssgi.quality = 16;
    ssgi.parameters[0] = {1.0f, 4.0f, 0.45f, 0.88f};
    ssgi.parameters[1] = {2.0f, 2.0f, 0.0f, 1.0f};
    ssgi.parameters[2] = {0.03f, 0.9f, 0.0f, 0.0f};
    measure("SSGI defaults", ssgi, "RHI SSGI / ", "RHI SSGI");
    for (const auto quality : {DdgiQuality::Low, DdgiQuality::Medium, DdgiQuality::High})
    {
        const auto settings = ApplyDdgiQuality(DdgiSettings{}, quality);
        const auto ddgi = EncodeDdgiPacket(settings, &settings);
        const auto label = std::string("DDGI ") + std::string(DdgiQualityNames[static_cast<std::size_t>(quality)]);
        measure(label.c_str(), ddgi, "RHI DDGI / ", "RHI DDGI");
    }
}
