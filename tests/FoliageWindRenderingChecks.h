#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/FoliageWind.h"
#include "PlutoGE/render/ShaderGraph.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Foliage wind on the GPU (docs/FOLIAGE_WIND.md). Tall, subdivided blades rendered through the real passes:
//  - In calm air a draw with wind enabled renders bit-identically to one without (wind costs a calm scene nothing).
//  - In wind the blade tips move downwind while the roots stay put, through both the instanced and single-object
//    vertex paths, opaque and alpha-masked.
//  - Swaying blades keep their coverage: the depth prepass deforms exactly as the shaded pass does, so its equal-depth
//    test discards nothing.
//  - The blades' shadows move downwind with them, with cascaded shadow maps and (where the backend has them) VSM.
//  - Beyond the wind's shadow sway distance, VSM casts the rest pose: the shadows stay where calm air leaves them.
// pageStatistics: whether the backend reads VSM page statistics back every frame (Vulkan). OpenGL's arrive
// irregularly, so the page-cache check runs only where they do.
template <class ReadPixels>
void CheckFoliageWind(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels, bool pageStatistics = true)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error("Foliage wind: " + message);
    };
    constexpr std::uint32_t Width = 128, Height = 96;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    require(renderer.Resize(Width, Height), "viewport resize failed");

    // A blade 0.4 wide and 2 tall in the XY plane, in eight rows so it bends smoothly; its root is the mesh origin.
    std::vector<BasicVertex> bladeVertices;
    std::vector<std::uint32_t> bladeIndices;
    constexpr int Rows = 8;
    for (int row = 0; row <= Rows; ++row)
        for (int side = 0; side < 2; ++side)
        {
            const float v = float(row) / Rows;
            bladeVertices.push_back({{side ? 0.2f : -0.2f, 2.0f * v, 0.0f}, {0, 0, 1}, {float(side), v}});
        }
    for (std::uint32_t row = 0; row < Rows; ++row)
    {
        const std::uint32_t a = row * 2;
        bladeIndices.insert(bladeIndices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
    }
    constexpr std::array<BasicVertex, 4> planeVertices = {{
        {{-1, 0, -1}, {0, 1, 0}, {0, 0}}, {{1, 0, -1}, {0, 1, 0}, {1, 0}}, {{1, 0, 1}, {0, 1, 0}, {1, 1}}, {{-1, 0, 1}, {0, 1, 0}, {0, 1}}}};
    constexpr std::array<std::uint32_t, 6> planeIndices{0, 2, 1, 0, 3, 2};
    auto blade = renderer.CreateMesh({bladeVertices, bladeIndices});
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});

    SceneWindSettings breeze;
    breeze.direction = {1.0f, 0.0f}; // toward +X, screen right in both views below
    breeze.speed = 20.0f;
    breeze.gustiness = 0.0f;         // a steady lean: the tips always sit downwind of the roots
    const auto response = FoliageWindPresetResponse(FoliageWindPreset::Grass, 2.0f);

    const auto luminance = [](const std::vector<std::uint8_t> &pixels, std::uint32_t x, std::uint32_t y) {
        const auto *p = &pixels[(std::size_t(y) * Width + x) * 4];
        return (int(p[0]) + int(p[1]) + int(p[2])) / 3;
    };
    const auto render = [&](const BasicLighting &lighting, const glm::mat4 &viewProjection, std::span<const BasicDraw> draws,
                            float time) {
        std::vector<std::uint8_t> pixels;
        // Several frames: temporal resolves and VSM page residency settle; the clock stays put, so the pose does too.
        for (int frame = 0; frame < 6; ++frame)
        {
            SetShaderGraphTimeSeconds(time);
            renderer.Render(viewProjection, lighting, draws);
            const auto raw = readPixels(renderer.GetColorTexture());
            pixels.resize(raw.size());
            std::transform(raw.begin(), raw.end(), pixels.begin(), [](auto value) { return static_cast<std::uint8_t>(value); });
        }
        require(pixels.size() == std::size_t(Width) * Height * 4, "readback size");
        return pixels;
    };

    // ---- Blades seen from the front, emissive on black: coverage is the bright pixels. ----
    BasicLighting front;
    front.shadowsEnabled = false;
    front.ambientIntensity = 0.0f;
    front.directionalIntensity = 0.0f;
    front.cameraPosition = {0.0f, 1.0f, 4.0f};
    front.view = glm::lookAtRH(front.cameraPosition, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0, 1, 0));
    const auto frontProjection = glm::perspectiveRH_ZO(glm::radians(60.0f), float(Width) / Height, 100.0f, 0.1f);
    const auto frontViewProjection = frontProjection * front.view;

    // Per row, the mean x of covered pixels (rows without coverage are NaN).
    const auto rowCentroids = [&](const std::vector<std::uint8_t> &pixels, std::size_t &covered) {
        std::vector<float> centroids(Height, std::numeric_limits<float>::quiet_NaN());
        covered = 0;
        for (std::uint32_t y = 0; y < Height; ++y)
        {
            float sum = 0.0f;
            int count = 0;
            for (std::uint32_t x = 0; x < Width; ++x)
                if (luminance(pixels, x, y) > 96) { sum += float(x); ++count; }
            covered += std::size_t(count);
            if (count) centroids[y] = sum / float(count);
        }
        return centroids;
    };

    for (const std::uint32_t alphaMode : {0u, 1u})
        for (const bool instanced : {true, false})
        {
            const std::string variant = std::string(instanced ? "instanced" : "single") + (alphaMode ? " masked" : " opaque");
            BasicDraw draw;
            draw.mesh = &blade;
            draw.baseColor = {0, 0, 0, 1};
            draw.emission = {1, 1, 1};
            draw.twoSided = true;
            draw.alphaMode = alphaMode;
            draw.castsShadow = false;
            if (instanced)
            {
                // Two blades side by side; the per-row centroid averages them, which shifts like either one.
                auto models = std::make_shared<std::vector<glm::mat4>>();
                models->push_back(glm::translate(glm::mat4(1), glm::vec3(-0.6f, 0.0f, 0.0f)));
                models->push_back(glm::translate(glm::mat4(1), glm::vec3(0.6f, 0.0f, 0.0f)));
                draw.instanceModels = models;
                draw.previousInstanceModels = models;
            }
            const auto still = [&](bool enabled) {
                auto copy = draw;
                copy.foliageWind = enabled ? response : FoliageWindResponse{};
                front.wind = {};
                return render(front, frontViewProjection, std::span(&copy, 1), 3.0f);
            };
            const auto rigid = still(false);
            require(still(true) == rigid, variant + ": calm air must render exactly as a draw without wind");

            auto swaying = draw;
            swaying.foliageWind = response;
            front.wind = breeze;
            const auto windy = render(front, frontViewProjection, std::span(&swaying, 1), 3.0f);
            front.wind = {};

            std::size_t rigidCoverage = 0, windyCoverage = 0;
            const auto before = rowCentroids(rigid, rigidCoverage);
            const auto after = rowCentroids(windy, windyCoverage);
            require(rigidCoverage > 120, variant + ": the blades must be visible");
            // The prepass and the shaded pass deform alike; a mismatch would discard most of the blade.
            require(windyCoverage * 4 > rigidCoverage * 3 && windyCoverage * 3 < rigidCoverage * 4,
                    variant + ": swaying blades must keep their coverage (" + std::to_string(windyCoverage) + " vs " +
                        std::to_string(rigidCoverage) + " pixels)");
            // Rows covered in both images: the root rows stay put and the tip rows move downwind (+X). Which end of the
            // image is the root depends on the backend's Y convention, so compare the extremes.
            float smallest = std::numeric_limits<float>::max(), largest = std::numeric_limits<float>::lowest();
            for (std::uint32_t y = 0; y < Height; ++y)
                if (!std::isnan(before[y]) && !std::isnan(after[y]))
                {
                    smallest = std::min(smallest, after[y] - before[y]);
                    largest = std::max(largest, after[y] - before[y]);
                }
            std::cout << "Foliage wind " << variant << ": tip shift " << largest << " px, root shift " << smallest << " px\n";
            require(std::abs(smallest) <= 1.5f, variant + ": the blade roots must stay planted");
            require(largest >= 4.0f, variant + ": the blade tips must lean downwind");
        }

    // ---- Shadows: blades edge-on to a camera looking straight down, lit at a slant so their shadows lie on the
    // ground toward -Z. In wind the shadows' far ends move downwind (+X) with the blade tips. ----
    BasicLighting top;
    top.shadowsEnabled = true;
    top.ambientIntensity = 0.05f;
    top.directionalIntensity = 3.0f;
    top.directionalDirection = glm::normalize(glm::vec3(0.0f, -1.0f, -0.8f));
    top.cameraPosition = {0.0f, 6.0f, -0.8f};
    top.view = glm::lookAtRH(top.cameraPosition, glm::vec3(0.0f, 0.0f, -0.8f), glm::vec3(0, 0, -1));
    const auto topViewProjection = glm::perspectiveRH_ZO(glm::radians(50.0f), float(Width) / Height, 100.0f, 0.1f) * top.view;
    BasicDraw ground{.mesh = &plane, .model = glm::scale(glm::mat4(1), glm::vec3(8.0f))};
    BasicDraw caster;
    caster.mesh = &blade;
    caster.twoSided = true;
    caster.baseColor = {0.02f, 0.02f, 0.02f, 1.0f};
    auto casterModels = std::make_shared<std::vector<glm::mat4>>();
    for (const float x : {-0.9f, 0.0f, 0.9f})
        casterModels->push_back(glm::translate(glm::mat4(1), glm::vec3(x, 0.0f, 0.0f)));
    caster.instanceModels = casterModels;
    caster.previousInstanceModels = casterModels;

    // The mean x of dark ground pixels: the shadows (the blades themselves are edge-on and cover almost nothing).
    const auto shadowCentroid = [&](const std::vector<std::uint8_t> &pixels, int &count) {
        int lit = 0;
        for (std::uint32_t y = 0; y < Height; ++y)
            for (std::uint32_t x = 0; x < Width; ++x)
                lit = std::max(lit, luminance(pixels, x, y));
        float sum = 0.0f;
        count = 0;
        for (std::uint32_t y = 0; y < Height; ++y)
            for (std::uint32_t x = 0; x < Width; ++x)
                if (luminance(pixels, x, y) < lit / 3) { sum += float(x); ++count; }
        return count ? sum / float(count) : 0.0f;
    };

    for (const auto method : {ShadowMethod::Cascaded, ShadowMethod::Virtual})
    {
        const char *name = method == ShadowMethod::Cascaded ? "cascaded" : "VSM";
        top.shadowMethod = method;
        const auto shade = [&](bool enabled, bool windy, SceneWindSettings wind = {}) {
            auto copy = caster;
            copy.foliageWind = enabled ? response : FoliageWindResponse{};
            top.wind = windy ? (wind.Calm() ? breeze : wind) : SceneWindSettings{};
            const std::array draws{ground, copy};
            auto pixels = render(top, topViewProjection, draws, 3.0f);
            top.wind = {};
            return pixels;
        };
        const auto rigid = shade(false, false);
        if (method == ShadowMethod::Virtual && !renderer.GetFrameStats().virtualShadowsActive)
        {
            std::cout << "Foliage wind: VSM unavailable on this backend; its shadow check is skipped\n";
            continue;
        }
        require(shade(true, false) == rigid, std::string(name) + ": calm shadows must match a draw without wind");
        const auto windy = shade(true, true);
        int rigidCount = 0, windyCount = 0;
        const float before = shadowCentroid(rigid, rigidCount), after = shadowCentroid(windy, windyCount);
        std::cout << "Foliage wind " << name << " shadows: " << rigidCount << " -> " << windyCount
                  << " shadowed pixels, centroid " << before << " -> " << after << " px\n";
        require(rigidCount > 60 && windyCount > 60, std::string(name) + ": the blades must cast shadows");
        require(after - before >= 1.0f, std::string(name) + ": shadows must move downwind with the blades");
        if (method == ShadowMethod::Virtual && !pageStatistics)
            std::cout << "Foliage wind: VSM page statistics are not read back every frame here; the cache check is skipped\n";
        if (method == ShadowMethod::Virtual && pageStatistics)
        {
            // Pages the swaying blades dirty while the clock runs. The blades are about 6 m from the camera, so a 1 m
            // shadow sway distance leaves them casting their rest pose: their pages stay cached.
            const auto dirtyWhileAnimating = [&](float swayDistance) {
                auto wind = breeze;
                wind.shadowSwayDistance = swayDistance;
                auto copy = caster;
                copy.foliageWind = response;
                top.wind = wind;
                const std::array draws{ground, copy};
                (void)render(top, topViewProjection, draws, 3.0f);
                std::uint32_t dirty = 0;
                for (int frame = 1; frame <= 8; ++frame)
                {
                    SetShaderGraphTimeSeconds(3.0f + 0.1f * float(frame));
                    renderer.Render(topViewProjection, top, draws);
                    dirty += renderer.GetFrameStats().virtualShadows.dirty;
                }
                top.wind = {};
                return dirty;
            };
            const auto everywhere = dirtyWhileAnimating(0.0f), beyond = dirtyWhileAnimating(1.0f);
            std::cout << "Foliage wind VSM dirty pages while animating: " << everywhere << " swaying everywhere, "
                      << beyond << " beyond the shadow sway distance\n";
            require(everywhere > 0, "VSM: swaying casters must redraw their pages");
            require(beyond == 0, "VSM: casters beyond the shadow sway distance must keep their pages cached");
        }
    }

    SetShaderGraphTimeSeconds(0.0f);
    renderer.Resize(previousWidth, previousHeight);
}
