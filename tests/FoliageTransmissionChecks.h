#pragma once
#include "GlobalIlluminationRenderingChecks.h"
#include "PlutoGE/render/DdgiPacket.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Thin translucent surfaces (docs/FOLIAGE_TRANSMISSION.md): light reaches a leaf's far side and passes through it, and
// global illumination treats canopies as partly see-through on both ray sources.
namespace FoliageTransmissionChecks
{
    using namespace PlutoGE::render;
    using GlobalIlluminationChecks::Box;
    using GlobalIlluminationChecks::MakeCube;
    using GlobalIlluminationChecks::MakeQuad;
    using GlobalIlluminationChecks::Require;
    using GlobalIlluminationChecks::Scene;

    inline glm::vec3 CentrePixel(const BasicRenderer &renderer, const std::vector<float> &image)
    {
        const std::size_t at = (std::size_t(renderer.GetHeight() / 2) * renderer.GetWidth() + renderer.GetWidth() / 2) * 4;
        return {image[at], image[at + 1], image[at + 2]};
    }

    inline double Luminance(glm::vec3 colour)
    {
        return 0.2126 * colour.r + 0.7152 * colour.g + 0.0722 * colour.b;
    }

    inline void Print(const char *name, glm::vec3 colour)
    {
        std::printf("  %-44s %.4f %.4f %.4f\n", name, colour.r, colour.g, colour.b);
    }

    inline void MakeLeaf(BasicDraw &leaf, float transmission)
    {
        leaf.twoSided = true;
        leaf.baseColor = {0.1f, 0.3f, 0.05f, 1.0f};
        leaf.roughness = 0.6f;
        leaf.diffuseTransmission = transmission;
        leaf.diffuseTransmissionColor = {0.4f, 0.8f, 0.1f};
    }
}

// Direct light through a leaf: a leaf facing the camera with the sun or a local light behind it.
inline void CheckFoliageDirectTransmission(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device)
{
    using namespace FoliageTransmissionChecks;
    Require(renderer.Resize(96, 64), "Foliage viewport resize failed");
    auto leafMesh = MakeQuad(renderer, {0, 0, 0}, {3, 0, 0}, {0, 3, 0});
    auto blockerMesh = MakeQuad(renderer, {0, 0, -3}, {4, 0, 0}, {0, 4, 0});
    BasicDraw leaf{.mesh = &leafMesh};
    BasicDraw blocker{.mesh = &blockerMesh};
    blocker.twoSided = true;
    blocker.castsShadow = true;
    leaf.castsShadow = true;

    Scene scene{renderer, device};
    scene.lighting.ambientIntensity = 0.0f;
    scene.lighting.contactShadowLength = 0.0f;
    scene.lighting.directionalIntensity = 8.0f;
    scene.Look({0, 0, 4}, {0, 0, 0});
    const auto centre = [&](std::span<const BasicDraw> draws) { return CentrePixel(renderer, scene.Render(draws, {}, 3)); };
    const auto render = [&](float transmission, bool shadows, glm::vec3 lightDirection, bool withBlocker = false)
    {
        MakeLeaf(leaf, transmission);
        scene.lighting.shadowsEnabled = shadows;
        scene.lighting.directionalDirection = glm::normalize(lightDirection);
        const std::array draws{leaf, blocker};
        return centre(std::span(draws).first(withBlocker ? 2 : 1));
    };

    std::cout << "Foliage direct transmission:\n";
    const glm::vec3 fromBehind{0.0f, -0.25f, 1.0f}, fromFront{0.0f, -0.25f, -1.0f};
    const auto opaqueBacklit = render(0.0f, false, fromBehind);
    const auto backlit = render(0.45f, false, fromBehind);
    const auto backlitShadowed = render(0.45f, true, fromBehind);
    const auto backlitBlocked = render(0.45f, true, fromBehind, true);
    const auto opaqueLit = render(0.0f, false, fromFront);
    const auto frontLit = render(0.45f, false, fromFront);
    Print("opaque, sun behind", opaqueBacklit);
    Print("transmissive, sun behind", backlit);
    Print("transmissive, sun behind, own shadow", backlitShadowed);
    Print("transmissive, sun behind a blocker", backlitBlocked);
    Print("opaque, sun in front", opaqueLit);
    Print("transmissive, sun in front", frontLit);

    Require(Luminance(opaqueBacklit) < 0.01, "An opaque leaf must not be lit from behind");
    Require(Luminance(backlit) > 0.1, "Sun behind a transmissive leaf must light its near side");
    Require(backlit.g > backlit.r * 1.5 && backlit.r > backlit.b, "Transmitted light must take the transmission colour");
    Require(Luminance(backlitShadowed) > 0.8 * Luminance(backlit), "A leaf must not shadow its own transmitted light");
    Require(Luminance(backlitBlocked) < 0.1 * Luminance(backlit), "A caster between the sun and a leaf must block transmission");
    Require(std::abs(Luminance(frontLit) / Luminance(opaqueLit) - 0.55) < 0.12,
            "Reflection must lose the share that transmission takes");
    // Bounded energy: the near side's and far side's diffuse never exceed what comes in (albedo 1 at most per channel).
    Require(backlit.g + frontLit.g < 8.0f / 3.14159f * 1.05f, "Transmission must not add energy");

    // A point light behind the leaf, with and without its shadow, the same way.
    scene.lighting.directionalIntensity = 0.0f;
    BasicPointLight point;
    point.position = {0, 0, -1.5f};
    point.range = 10.0f;
    point.intensity = 6.0f;
    point.color = glm::vec3(1.0f);
    scene.lighting.pointLights = {point};
    const auto pointOpaque = render(0.0f, false, fromBehind);
    const auto pointThrough = render(0.45f, false, fromBehind);
    Print("opaque, point light behind", pointOpaque);
    Print("transmissive, point light behind", pointThrough);
    scene.lighting.pointLights.clear();
    Require(Luminance(pointOpaque) < 0.01, "An opaque leaf must not be lit by a light behind it");
    Require(Luminance(pointThrough) > 0.03, "A point light must shine through a transmissive leaf");
}

// A horizontal canopy above the ground under a sky of flat ambient light, seen from below. `source` is the ray source.
// Solid layers must keep almost all sky from the ground; foliage layers must let much of it through, without changing
// what solid surfaces block.
inline void CheckFoliageCanopyGi(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                                 PlutoGE::render::DdgiRaySource source)
{
    using namespace FoliageTransmissionChecks;
    Require(renderer.Resize(96, 64), "Foliage viewport resize failed");
    auto cube = MakeCube(renderer);
    auto layerMesh = MakeQuad(renderer, {0, 0, 0}, {24, 0, 0}, {0, 0, -24});

    Scene scene{renderer, device};
    scene.lighting.shadowsEnabled = false;
    scene.lighting.directionalIntensity = 0.0f;
    scene.lighting.ambientIntensity = 1.0f;
    scene.lighting.contactShadowLength = 0.0f;
    scene.Look({0, 1.6f, 6}, {0, 0.2f, 0});

    DdgiSettings settings;
    settings.grid.counts = {16, 8, 16};
    settings.grid.spacing = 1.5f;
    settings.grid.blendedRays = 128;
    settings.grid.probesPerFrame = DdgiGridSettings::MaxProbesPerFrame;
    settings.hysteresis = 0.8f;
    settings.stableHysteresis = 0.8f;
    settings.raySource = source;
    settings.voxels = {.resolution = 64, .cascades = 2, .volumeSize = 32.0f, .updateSpeed = 4.0f,
                       .injectLocalLights = false, .commandBudget = 256};

    const auto measure = [&](float transmission, int layers)
    {
        std::vector<BasicDraw> draws{Box(cube, {-30, -0.5f, -30}, {30, 0, 30})};
        for (int layer = 0; layer < layers; ++layer)
        {
            // Solid layers are closed boxes: rays meeting a one-sided quad's back would take its probes to be inside.
            if (transmission <= 0.0f)
            {
                const float height = 6.0f + float(layer) * 1.2f;
                draws.push_back(Box(cube, {-24, height, -24}, {24, height + 0.4f, 24}));
                continue;
            }
            BasicDraw leaves{.mesh = &layerMesh};
            leaves.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 6.0f + float(layer) * 1.2f, 0.0f));
            MakeLeaf(leaves, transmission);
            leaves.meanCoverage = 0.5f;
            draws.push_back(leaves);
        }
        // Another owner discards the previous measurement's probes.
        const auto discard = EncodeDdgiPacket(settings, &scene);
        (void)scene.Render(draws, std::span(&discard, 1), 1);
        const auto packet = EncodeDdgiPacket(settings, &settings);
        const auto base = scene.Render(draws, {}, 2);
        const auto lit = scene.Render(draws, std::span(&packet, 1), 96);
        const auto stats = renderer.GetFrameStats();
        Require(stats.ddgiProbesTraced > 0, "Foliage GI traced no probes");
        return GlobalIlluminationChecks::LitMeans(base, lit)[1];
    };

    const char *name = source == DdgiRaySource::Hardware ? "hardware" : "voxels";
    const double open = measure(0.0f, 0);
    const double solid = measure(0.0f, 2);
    const double foliage = measure(0.35f, 2);
    std::printf("Foliage GI (%s): open sky %.4f, solid canopy %.4f, foliage canopy %.4f\n", name, open, solid, foliage);
    Require(solid < 0.5 * open, "A solid canopy must block the sky");
    Require(foliage > 1.5 * solid, "A foliage canopy must let more light through than a solid one");
    Require(foliage < 0.97 * open, "A foliage canopy must still shade what lies under it");
}
