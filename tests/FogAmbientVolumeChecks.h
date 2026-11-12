#pragma once
#include "PlutoGE/render/AmbientProbeConfig.h"
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

// Fog inside baked ambient volumes (FogAmbientVolumes.slang). Fog lit only by the sky fills the view. A volume baked
// with sky visibility that encloses all of it must darken it when its probes see no sky, and change nothing when they
// see the whole sky. A volume baked without sky visibility, or one behind the camera, must change nothing.
template <class ReadPixels>
void CheckFogAmbientVolumes(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                            ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    constexpr int width = 96, height = 64;
    renderer.Resize(width, height);
    BasicLighting lighting;
    lighting.cameraPosition = {0, 1, 0};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, 1, -1), glm::vec3(0, 1, 0));
    lighting.ambientIntensity = 0.35f;
    lighting.directionalIntensity = 0.0f;
    lighting.shadowsEnabled = false;
    glm::mat4 projection(0);
    projection[1][1] = 1.0f / std::tan(glm::radians(35.0f));
    projection[0][0] = projection[1][1] * float(height) / float(width);
    projection[2][2] = .1f / (2000.f - .1f);
    projection[2][3] = -1;
    projection[3][2] = 2000.f * .1f / (2000.f - .1f);

    BasicPostProcessEffect fog{BasicPostProcessEffectType::VolumetricFog};
    fog.quality = 32;
    fog.parameters[0] = {1, 1, 1, .1f}; // dense enough that almost all of it lies within the 40 m lit range
    fog.parameters[1] = {0, 0, 40, 1};
    fog.parameters[2] = {0, 1, 0, 1}; // sky light only
    fog.parameters[3] = {1, 0, 0, 0};

    // Eight probes (a 2x2x2 grid) whose sky visibility share is skyShare.
    const auto atlasWith = [&](float skyShare)
    {
        constexpr int probes = 8;
        std::vector<float> texels(static_cast<std::size_t>(probes) * PLUTO_AMBIENT_PROBE_ROWS * 4, 0.0f);
        for (int probe = 0; probe < probes; ++probe)
        {
            const auto texel = [&](int row) { return (static_cast<std::size_t>(row) * probes + probe) * 4; };
            texels[texel(0) + 3] = 1.0f; // a valid probe (its position is not read by fog)
            texels[texel(2) + 3] = skyShare;
        }
        std::vector<std::byte> bytes(texels.size() * sizeof(float));
        std::memcpy(bytes.data(), texels.data(), bytes.size());
        return rhi::Texture(device, device.CreateTexture({probes, PLUTO_AMBIENT_PROBE_ROWS, rhi::Format::R32G32B32A32Float,
                                                          rhi::TextureUsage::Sampled, "Fog ambient volume atlas", false},
                                                         bytes));
    };
    const auto useVolume = [&](const rhi::Texture *atlas, glm::vec3 origin, glm::vec3 size, bool hasSkyVisibility)
    {
        lighting.diffuseProbeAtlas = atlas ? atlas->Get() : rhi::TextureHandle{};
        lighting.diffuseProbeAtlasWidth = 8;
        lighting.diffuseProbeVolumeCount = atlas ? 1 : 0;
        lighting.diffuseProbeVolumesPrepared = true;
        auto &volume = lighting.diffuseProbeVolumes[0];
        volume = {};
        volume.origin = glm::vec4(origin, 0.0f);
        volume.size = glm::vec4(size, 0.0f);
        volume.resolutionBase = {2, 2, 2, 0};
        volume.worldToGridRows[2].w = hasSkyVisibility ? 1.0f : 0.0f;
    };
    // Mean red over the view, after the froxel history has converged.
    const auto brightness = [&]
    {
        decltype(readPixels(renderer.GetColorTexture())) pixels;
        for (int frame = 0; frame < 40; ++frame)
        {
            renderer.Render(projection * lighting.view, lighting, std::span<const BasicDraw>{}, std::span(&fog, 1));
            pixels = readPixels(renderer.GetColorTexture());
        }
        double sum = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4)
            sum += static_cast<unsigned char>(pixels[pixel]) / 255.0;
        return sum / (pixels.size() / 4);
    };

    const glm::vec3 aroundOrigin{-3000, -1000, -3000}, aroundSize{6000, 3000, 6000}; // encloses all the fog in view
    const glm::vec3 behindOrigin{-50, -50, 20}, behindSize{100, 100, 100};          // behind the camera
    const auto enclosedAtlas = atlasWith(0.0f);
    const auto openAtlas = atlasWith(1.0f);
    useVolume(nullptr, aroundOrigin, aroundSize, true);
    const double none = brightness();
    useVolume(&enclosedAtlas, aroundOrigin, aroundSize, true);
    const double enclosed = brightness();
    useVolume(&openAtlas, aroundOrigin, aroundSize, true);
    const double openSky = brightness();
    useVolume(&enclosedAtlas, aroundOrigin, aroundSize, false);
    const double unbaked = brightness();
    useVolume(&enclosedAtlas, behindOrigin, behindSize, true);
    const double behind = brightness();
    useVolume(nullptr, aroundOrigin, aroundSize, true);
    std::cout << "Fog in ambient volumes: no volume " << none << ", enclosed " << enclosed << ", open sky " << openSky
              << ", without sky visibility " << unbaked << ", volume behind the camera " << behind << '\n';
    const auto near = [](double a, double b) { return std::abs(a - b) <= 0.005 + 0.03 * std::max(a, b); };
    if (none < 0.05)
        throw std::runtime_error("Fog ambient volume check has too little sky-lit fog to measure");
    if (!(enclosed < none * 0.1))
        throw std::runtime_error("Fog inside a volume whose probes see no sky was not darkened");
    if (!near(openSky, none))
        throw std::runtime_error("Fog where the probes see the whole sky changed");
    if (!near(unbaked, none))
        throw std::runtime_error("A volume baked without sky visibility changed the fog");
    if (!near(behind, none))
        throw std::runtime_error("A volume behind the camera changed the fog in front of it");
    std::cout << "Fog ambient volume checks passed\n";
}
