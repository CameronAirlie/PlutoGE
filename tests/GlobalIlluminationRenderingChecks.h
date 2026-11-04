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

    // An axis-aligned unit cube centred on the origin, faces outwards; scale it with a draw's model matrix.
    inline BasicMesh MakeCube(BasicRenderer &renderer)
    {
        std::vector<BasicVertex> vertices;
        std::vector<std::uint32_t> indices;
        for (int axis = 0; axis < 3; ++axis)
            for (const float side : {-1.0f, 1.0f})
            {
                glm::vec3 normal(0.0f), u(0.0f), v(0.0f);
                normal[axis] = side;
                u[(axis + 1) % 3] = 0.5f;
                v[(axis + 2) % 3] = 0.5f;
                if (side < 0.0f) std::swap(u, v);
                const glm::vec3 center = normal * 0.5f;
                const auto base = static_cast<std::uint32_t>(vertices.size());
                for (const auto &corner : {center - u - v, center + u - v, center + u + v, center - u + v})
                {
                    BasicVertex vertex{};
                    vertex.position = {corner.x, corner.y, corner.z};
                    vertex.normal = {normal.x, normal.y, normal.z};
                    vertices.push_back(vertex);
                }
                for (const std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u}) indices.push_back(base + index);
            }
        return renderer.CreateMesh({vertices, indices});
    }

    // A draw of `cube` (MakeCube) spanning `minimum` to `maximum`.
    inline BasicDraw Box(const BasicMesh &cube, glm::vec3 minimum, glm::vec3 maximum)
    {
        BasicDraw draw{.mesh = &cube};
        draw.model = glm::scale(glm::translate(glm::mat4(1.0f), (minimum + maximum) * 0.5f), maximum - minimum);
        draw.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
        return draw;
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

// Hardware rays rebuild the top level only when the traced draws change: retained packets (revisions) that stay the
// same reuse it, a changed surface or a new packet rebuilds it, and draws below the size threshold are left out.
// Devices without hardware ray tracing skip it.
inline void CheckRayTracedSceneReuse(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device)
{
    using namespace GlobalIlluminationChecks;
    Require(renderer.Resize(96, 64), "Ray-traced scene viewport resize failed");
    auto floorMesh = MakeQuad(renderer, {0, 0, 0}, {6, 0, 0}, {0, 0, -6});
    auto wallMesh = MakeQuad(renderer, {0, 1.5f, -1}, {4, 0, 0}, {0, 1.5f, 0});
    BasicDraw floor{.mesh = &floorMesh, .preparationRevision = 0x7001};
    BasicDraw wall{.mesh = &wallMesh, .preparationRevision = 0x7002};
    wall.emission = {2.0f, 0.0f, 0.0f};
    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = false;
    scene.Look({0, 1.0f, 3.5f}, {0, 0.3f, 0});
    DdgiSettings settings;
    settings.raySource = DdgiRaySource::Hardware;
    const auto ddgi = EncodeDdgiPacket(settings, &settings);
    std::array draws{floor, wall};
    (void)scene.Render(draws, std::span(&ddgi, 1), 3);
    if (!renderer.GetFrameStats().ddgiHardwareRays)
    {
        std::cout << "Ray-traced scene reuse skipped: no hardware ray tracing\n";
        return;
    }
    Require(renderer.GetFrameStats().rayTracedInstances == 2 && !renderer.GetFrameStats().rayTracedSceneRebuilt,
            "An unchanged ray-traced scene rebuilt its top level");
    draws[1].emission = {0.0f, 2.0f, 0.0f};
    (void)scene.Render(draws, std::span(&ddgi, 1), 1);
    Require(renderer.GetFrameStats().rayTracedSceneRebuilt, "A changed hit surface reused the old top level");
    draws[1].preparationRevision = 0x7003;
    (void)scene.Render(draws, std::span(&ddgi, 1), 1);
    Require(renderer.GetFrameStats().rayTracedSceneRebuilt, "A new packet reused the old top level");
    scene.lighting.rayTracedMinimumInstanceRadius = 2.0f;
    draws[0].maxInstanceRadius = 8.0f;
    draws[1].maxInstanceRadius = 1.0f;
    (void)scene.Render(draws, std::span(&ddgi, 1), 1);
    const auto &stats = renderer.GetFrameStats();
    Require(stats.rayTracedSceneRebuilt && stats.rayTracedInstances == 1 && stats.rayTracedSkippedInstances == 1,
            "The ray-traced size threshold did not leave out exactly the small draw");

    // Draws beyond the probe volume's reach are left out, and a camera step within the snapped centre's cell keeps
    // the top level.
    scene.lighting.rayTracedMinimumInstanceRadius = 0.0f;
    auto distant = floor;
    distant.preparationRevision = 0x7004;
    distant.model = glm::translate(glm::mat4(1.0f), glm::vec3(20000.0f, 0.0f, 0.0f));
    distant.shadowBoundsCenter = glm::vec3(20000.0f, 0.0f, 0.0f);
    distant.shadowBoundsRadius = 5.0f;
    const std::array ranged{draws[0], draws[1], distant};
    (void)scene.Render(ranged, std::span(&ddgi, 1), 2);
    Require(renderer.GetFrameStats().rayTracedInstances == 2 && renderer.GetFrameStats().rayTracedOutOfRangeInstances == 1,
            "A draw beyond the probe volume's reach was traced");
    scene.Look({0.05f, 1.0f, 3.5f}, {0.05f, 0.3f, 0});
    (void)scene.Render(ranged, std::span(&ddgi, 1), 1);
    Require(!renderer.GetFrameStats().rayTracedSceneRebuilt, "A small camera step rebuilt the ray-traced scene");
    std::cout << "Ray-traced scene reuse, invalidation, size threshold and range passed\n";
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

    // Stable hysteresis only holds changes within the rays' noise: switching the emitter off is far beyond it and must
    // fade as quickly as without it.
    {
        const auto fadeFrames = [&](float stableHysteresis)
        {
            auto stable = settings;
            stable.stableHysteresis = stableHysteresis;
            const auto packet = EncodeDdgiPacket(stable, &stable);
            (void)scene.Render(bounceScene, std::span(&packet, 1), ConvergenceFrames);
            auto dark = wall;
            dark.emission = glm::vec3(0.0f);
            const std::array darkScene{floor, dark};
            const auto darkBase = scene.Render(darkScene, {}, 2);
            int frames = 0;
            while (frames < 4 * ConvergenceFrames &&
                   RedBleed(darkBase, scene.Render(darkScene, std::span(&packet, 1), 1)) > 10)
                ++frames;
            return frames;
        };
        const int responsive = fadeFrames(settings.hysteresis), stable = fadeFrames(0.99f);
        if (stable > responsive + 2)
            std::cerr << "DDGI emitter fade frames: " << responsive << " responsive, " << stable << " stable\n";
        Require(stable <= responsive + 2, "DDGI stable hysteresis delayed a real lighting change");
    }

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

    // Reflection occlusion: a glossy metal floor reflects the physical sky wherever SSR has no hit. Under a roof the
    // probes see no sky along the reflection, so the sky reflection must give way to the probes' dimmer light; on open
    // ground they see the sky and the reflection must stay. Both sides compare DDGI with and without occlusion, so
    // its diffuse lighting cancels.
    double roofedReflection = 0.0, openReflection = 0.0;
    {
        scene.lighting.ambientIntensity = 0.0f;
        scene.lighting.physicalSkyEnabled = true;
        scene.lighting.physicalSkyExposure = 1.0f;
        scene.lighting.physicalSkyParameters[0] = glm::vec4(glm::normalize(glm::vec3(0.5f, 0.6f, 0.3f)), 1);
        scene.lighting.physicalSkyParameters[1] = {1, 0.98f, 0.95f, 0.65f};
        scene.lighting.physicalSkyParameters[2] = {0.55f, 0.65f, 0.95f, 0.65f};
        scene.lighting.physicalSkyParameters[3] = {0.012f, 0.015f, 0.02f, 1};
        scene.lighting.physicalSkyParameters[4] = {8, 0.27f, 0.035f, 0};
        scene.lighting.physicalSkyParameters[5] = {0.8f, 0.26f, 0, 0};
        auto mirrorFloor = greyFloor;
        mirrorFloor.baseColor = {0.9f, 0.9f, 0.9f, 1.0f};
        mirrorFloor.metallic = 1.0f;
        mirrorFloor.roughness = 0.15f;
        // The camera looks under the roof from beside it, so the reflections it sees point up into the roof.
        scene.Look({0, 0.6f, 4.5f}, {0, 0.2f, -1.0f});
        const auto reflectionRatio = [&](std::span<const BasicDraw> draws)
        {
            auto unoccluded = settings, occluded = settings;
            unoccluded.reflectionOcclusion = 0.0f;
            occluded.reflectionOcclusion = 1.0f;
            const auto withoutPacket = EncodeDdgiPacket(unoccluded, &unoccluded);
            const auto withPacket = EncodeDdgiPacket(occluded, &occluded);
            const auto without = scene.Render(draws, std::span(&withoutPacket, 1), ConvergenceFrames);
            const auto with = scene.Render(draws, std::span(&withPacket, 1), ConvergenceFrames);
            const auto means = LitMeans(without, with);
            return means[1] / means[0];
        };
        const std::array roofedMirror{mirrorFloor, roof};
        roofedReflection = reflectionRatio(roofedMirror);
        if (roofedReflection >= 0.85) std::cerr << "DDGI roofed reflection ratio: " << roofedReflection << '\n';
        Require(roofedReflection < 0.85, "DDGI did not occlude the sky reflected under a roof");
        const std::array openMirror{mirrorFloor};
        openReflection = reflectionRatio(openMirror);
        if (openReflection <= 0.93) std::cerr << "DDGI open reflection ratio: " << openReflection << '\n';
        Require(openReflection > 0.93, "DDGI occluded the sky reflected by open ground");

        for (const int view : {PLUTO_DDGI_VIEW_SKY_VISIBILITY, PLUTO_DDGI_VIEW_REFLECTIONS})
        {
            settings.debugView = view;
            ddgi = EncodeDdgiPacket(settings, &settings);
            (void)scene.Render(roofedMirror, std::span(&ddgi, 1), 2);
        }
        scene.lighting.physicalSkyEnabled = false;
        scene.lighting.ambientIntensity = 1.0f;
    }

    for (const int view : {PLUTO_DDGI_VIEW_IRRADIANCE, PLUTO_DDGI_VIEW_INDIRECT, PLUTO_DDGI_VIEW_PROBES})
    {
        settings.debugView = view;
        ddgi = EncodeDdgiPacket(settings, &settings);
        (void)scene.Render(roofScene, std::span(&ddgi, 1), 2);
    }

    std::cout << "DDGI bounce light (" << bled << " pixels), open ambient ratio " << openRatio
              << ", roofed ambient ratio " << roofRatio << ", reflection ratio roofed " << roofedReflection
              << " / open " << openReflection << " and debug views passed\n";
}

// A probe classified inactive traces only its fixed rays; its other ray slots keep whatever was traced into them
// before. When geometry appears beside it, classification reactivates it after that partial trace, and it must not
// blend those slots: they once replaced its history with rays from the scene as it was when the probe last traced
// them (DDGIProbeUpdate.slang, PLUTO_DDGI_PROBE_PARTIAL_RAYS).
//
// The probes high above a red emissive floor trace it once, then go inactive (no surface within reach). The floor
// stops emitting and everything settles; a grey wall then appears beside those probes. Lighting it with red would be
// stale rays: nothing in the scene is red any more.
inline void CheckDdgiProbeReactivation(PlutoGE::render::BasicRenderer &renderer,
                                       PlutoGE::render::rhi::IRenderDevice &device)
{
    using namespace GlobalIlluminationChecks;
    Require(renderer.Resize(96, 64), "DDGI reactivation viewport resize failed");
    auto floorMesh = MakeQuad(renderer, {0, 0, 0}, {6, 0, 0}, {0, 0, -6});
    auto wallMesh = MakeQuad(renderer, {0, 2.2f, -1}, {3, 0, 0}, {0, 0.6f, 0});
    BasicDraw floor{.mesh = &floorMesh};
    BasicDraw wall{.mesh = &wallMesh};
    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = false;
    scene.lighting.directionalIntensity = 0;
    scene.lighting.ambientIntensity = 1.0f;
    scene.Look({0, 1.6f, 3.5f}, {0, 2.0f, -1});

    DdgiSettings settings;
    settings.grid.counts = {16, 6, 24};
    settings.grid.spacing = 0.75f;
    settings.grid.blendedRays = 64;
    settings.grid.probesPerFrame = DdgiGridSettings::MaxProbesPerFrame;
    settings.hysteresis = 0.5f;
    settings.stableHysteresis = 0.5f;
    // Bounces off the floor would carry its red history into the wall's probes legitimately.
    settings.multipleBounces = false;
    settings.voxels = {.resolution = 64, .cascades = 2, .volumeSize = 24.0f, .updateSpeed = 4.0f,
                       .injectLocalLights = false, .secondaryBounce = 0.0f, .commandBudget = 256};
    const auto ddgi = EncodeDdgiPacket(settings, &settings);

    // Share of the image's light that is red without matching green.
    const auto redExcess = [](const std::vector<float> &image)
    {
        double red = 0.0, total = 0.0;
        for (std::size_t i = 0; i < image.size(); i += 4)
        {
            red += std::max(image[i] - image[i + 1], 0.0f);
            total += image[i + 1];
        }
        return total > 0.0 ? red / total : 0.0;
    };

    floor.emission = {4.0f, 0.0f, 0.0f};
    const std::array emitting{floor};
    (void)scene.Render(emitting, std::span(&ddgi, 1), 8);
    floor.emission = glm::vec3(0.0f);
    const std::array settled{floor};
    const double before = redExcess(scene.Render(settled, std::span(&ddgi, 1), 64));
    const std::array walled{floor, wall};
    double peak = 0.0;
    for (int frame = 0; frame < 24; ++frame)
        peak = std::max(peak, redExcess(scene.Render(walled, std::span(&ddgi, 1), 1)));
    // Blending the stale slots peaked at 0.09.
    if (before >= 0.01 || peak >= 0.01)
        std::cerr << "DDGI reactivation red share before the wall " << before << ", peak after " << peak << '\n';
    Require(before < 0.01, "DDGI kept red light after the emitter went dark");
    Require(peak < 0.01, "DDGI reactivated probes blended rays they did not trace");
    std::cout << "DDGI probe reactivation passed (red share peak " << peak << ")\n";
}

namespace GlobalIlluminationChecks
{
    // A sealed room under open sky, built as Basin's buildings are: walls, roof and floor 0.3 thick, under a voxel at
    // the default quality. No light can reach its interior, so the light DDGI gives it is leakage, measured as a share
    // of the unoccluded ambient light the geometry pass gives it without DDGI. `inner` is the room's half extent in x
    // and z and its height; `offset` moves it against the probe grid, since leaks depend on where surfaces fall
    // between probes.
    inline double MeasureDdgiLeak(Scene &scene, const BasicMesh &cube, const DdgiSettings &settings, glm::vec3 offset,
                                  glm::vec3 inner)
    {
        constexpr float Thickness = 0.3f;
        constexpr int Frames = 96;
        const auto at = [&](glm::vec3 minimum, glm::vec3 maximum) { return Box(cube, minimum + offset, maximum + offset); };
        const std::array room{
            at({-30, -Thickness, -30}, {30, 0, 30}), // ground and floor
            at({-inner.x - Thickness, 0, -inner.z - Thickness}, {-inner.x, inner.y, inner.z + Thickness}),
            at({inner.x, 0, -inner.z - Thickness}, {inner.x + Thickness, inner.y, inner.z + Thickness}),
            at({-inner.x, 0, -inner.z - Thickness}, {inner.x, inner.y, -inner.z}),
            at({-inner.x, 0, inner.z}, {inner.x, inner.y, inner.z + Thickness}),
            at({-inner.x - Thickness, inner.y, -inner.z - Thickness},
               {inner.x + Thickness, inner.y + Thickness, inner.z + Thickness})}; // roof
        // From one inside corner towards the opposite one, seeing floor, ceiling and two walls.
        scene.Look(offset + glm::vec3(-0.75f * inner.x, 0.45f * inner.y, 0.75f * inner.z),
                   offset + glm::vec3(0.75f * inner.x, 0.35f * inner.y, -0.75f * inner.z));
        // A frame under another owner discards the probes' history, so each measurement starts from scratch.
        const auto discard = EncodeDdgiPacket(settings, &scene);
        (void)scene.Render(room, std::span(&discard, 1), 1);
        const auto packet = EncodeDdgiPacket(settings, &settings);
        const auto base = scene.Render(room, {}, 2);
        const auto means = LitMeans(base, scene.Render(room, std::span(&packet, 1), Frames));
        return means[1] / means[0];
    }
}

// Light leaking into sealed rooms (MeasureDdgiLeak) at the default quality, with walls at several placements against
// the probe grid. A room wider than two probe spacings keeps probes inside it; one narrower than that (2.4 units
// against a spacing of 2) has every probe around it outside, where standard DDGI normalisation lit it with 35-65% of
// the outside light. `report` prints every measurement (PlutoGEVulkanRhiTests --ddgi-leakage).
inline void CheckDdgiLeakage(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                             bool report = false)
{
    using namespace GlobalIlluminationChecks;
    Require(renderer.Resize(128, 80), "DDGI leakage viewport resize failed");
    auto cube = MakeCube(renderer);
    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = false;
    scene.lighting.directionalIntensity = 0;
    scene.lighting.ambientIntensity = 1.0f;
    auto settings = ApplyDdgiQuality(DdgiSettings{}, DdgiQuality::Medium);
    settings.grid.probesPerFrame = DdgiGridSettings::MaxProbesPerFrame;
    settings.voxels.updateSpeed = 4.0f;
    settings.voxels.commandBudget = 256;
    const glm::vec3 wideRoom(4.0f, 3.6f, 4.0f), narrowRoom(1.2f, 2.6f, 1.2f);
    const std::array offsets{glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.5f, 0.25f, 0.5f),
                             glm::vec3(1.5f, 1.0f, 0.75f), glm::vec3(0.2f, 1.8f, 1.2f)};
    double wideWorst = 0.0, narrowMean = 0.0;
    for (const bool thin : {false, true})
    {
        settings.relocateFromThinSurfaces = thin;
        for (const auto inner : {wideRoom, narrowRoom})
        {
            if (report) std::cout << "DDGI leak, room " << 2.0f * inner.x << ", thin relocation " << thin << ':';
            for (const auto offset : offsets)
            {
                const double leak = MeasureDdgiLeak(scene, cube, settings, offset, inner);
                if (report) std::cout << ' ' << leak;
                if (inner == wideRoom) wideWorst = std::max(wideWorst, leak);
                else narrowMean += leak / (2.0 * offsets.size());
            }
            if (report) std::cout << '\n';
        }
    }
    // Without leak reduction the narrow room shows what the check guards against.
    settings.relocateFromThinSurfaces = false;
    settings.leakReduction = 0.0f;
    const double unreduced = MeasureDdgiLeak(scene, cube, settings, offsets[0], narrowRoom);
    // Measured: wide rooms below 0.0012 at every placement (thin relocation reached 0.04 before the occlusion volume),
    // the narrow room 0.05 on average; without leak reduction 0.49 when written, 0.24 on the Vega 8 (voxel ray source).
    // Without leak reduction the narrow room must leak past its pass bound (0.12), so the check still sees leaks.
    if (wideWorst >= 0.005 || narrowMean >= 0.12 || unreduced <= 0.15)
        std::cerr << "DDGI leak: wide room worst " << wideWorst << ", narrow room mean " << narrowMean
                  << ", narrow room without leak reduction " << unreduced << '\n';
    Require(unreduced > 0.15, "DDGI leak check no longer sees light leak into a narrow room");
    Require(wideWorst < 0.005, "DDGI leaked light into a sealed room");
    Require(narrowMean < 0.12, "DDGI leaked light into a sealed room narrower than the probe spacing");
    std::cout << "DDGI leakage passed (wide room worst " << wideWorst << ", narrow room mean " << narrowMean
              << ", " << unreduced << " without leak reduction)\n";
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
