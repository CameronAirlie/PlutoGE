#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/DetailGrass.h"
#include "PlutoGE/render/ShaderGraph.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Detail grass on the GPU (docs/DETAIL_GRASS.md), through the real passes: a flat 32 m terrain whose density map is
// painted on its left half only, seen from above and behind.
//  - Grass grows where the map is painted and not where it is not, and it covers the ground there.
//  - The field is a pure function of the camera: identical frames render identically, and the generated clump count
//    read back is stable.
//  - Calm air with wind enabled renders exactly as wind disabled; a disabled layer draws nothing.
//  - Moving the camera a little keeps the field (clumps are anchored to the world, not the view).
template <class ReadPixels>
void CheckDetailGrass(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error("Detail grass: " + message);
    };
    require(renderer.DetailGrassAvailable(), "the renderer has no detail grass (shaders or device)");
    constexpr std::uint32_t Width = 160, Height = 96;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    require(renderer.Resize(Width, Height), "viewport resize failed");

    // Ground: a grey plane under the terrain's 32 m square.
    constexpr std::array<BasicVertex, 4> groundVertices = {{
        {{-16, 0, -16}, {0, 1, 0}, {0, 0}}, {{16, 0, -16}, {0, 1, 0}, {1, 0}}, {{16, 0, 16}, {0, 1, 0}, {1, 1}}, {{-16, 0, 16}, {0, 1, 0}, {0, 1}}}};
    constexpr std::array<std::uint32_t, 6> groundIndices{0, 2, 1, 0, 3, 2};
    auto groundMesh = renderer.CreateMesh({groundVertices, groundIndices});
    BasicDraw ground;
    ground.mesh = &groundMesh;
    ground.baseColor = {0.25f, 0.25f, 0.25f, 1.0f};
    ground.castsShadow = false;

    // The terrain: flat, 33 x 33 samples a metre apart, centred on the origin. Its density map is 4 x 1 texels, painted
    // (red) on its left two: the field grows for world x below about 0.
    auto heights = std::make_shared<DetailGrassHeightField>();
    heights->columns = heights->rows = 33;
    heights->cellSize = 1.0f;
    heights->heights.assign(33 * 33, 0.0f);
    heights->revision = 1;
    const std::array<std::uint8_t, 16> painted{255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    rhi::Texture density(device, device.CreateTexture({4, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled,
                                                       "Detail grass test density", true, 1, false, 1},
                                                      std::as_bytes(std::span(painted))));

    BasicDetailGrassField field;
    field.id = 1;
    field.terrainToWorld = glm::translate(glm::mat4(1.0f), glm::vec3(-16.0f, 0.0f, -16.0f));
    field.heights = heights;
    field.density = density.Get();
    field.densityChannel = {1.0f, 0.0f, 0.0f, 0.0f};
    field.layer.color = {0.1f, 0.7f, 0.08f};
    field.layer.dryAmount = 0.0f;
    field.layer.colorJitter = 0.0f;
    field.layer.groundTint = 0.0f;
    field.layer.minHeight = 0.5f;
    field.layer.maxHeight = 0.8f;
    field.layer.density = 18.0f;
    field.layer.fullDensityDistance = 10.0f;
    field.layer.drawDistance = 40.0f;
    field.layer.wind.enabled = false;

    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.cameraPosition = {0.0f, 4.0f, 9.0f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(70.0f), float(Width) / Height, 200.0f, 0.1f);

    // The ground's pixels, from a render without grass (readbacks differ in row order between backends).
    std::vector<bool> groundMask;
    const auto isGround = [](const std::vector<std::uint8_t> &pixels, std::size_t pixel) {
        const auto *p = &pixels[pixel * 4];
        return int(p[0]) + int(p[1]) + int(p[2]) > 60 && std::abs(int(p[0]) - int(p[2])) < 12;
    };
    // Shares of the ground's pixels that are green in the left and right halves of the image.
    const auto coverage = [&](const std::vector<std::uint8_t> &pixels) {
        std::array<float, 2> green{}, total{};
        for (std::uint32_t y = 0; y < Height; ++y)
            for (std::uint32_t x = 0; x < Width; ++x)
            {
                const auto pixel = std::size_t(y) * Width + x;
                if (!groundMask[pixel]) continue;
                const auto *p = &pixels[pixel * 4];
                const bool grass = int(p[1]) > int(p[0]) + 12 && int(p[1]) > int(p[2]) + 12;
                total[x < Width / 2 ? 0 : 1] += 1.0f;
                green[x < Width / 2 ? 0 : 1] += grass ? 1.0f : 0.0f;
            }
        return std::array<float, 2>{green[0] / std::max(total[0], 1.0f), green[1] / std::max(total[1], 1.0f)};
    };
    const auto render = [&](const BasicLighting &view, std::span<const BasicDetailGrassField> fields, int frames = 4) {
        std::vector<std::uint8_t> pixels;
        for (int frame = 0; frame < frames; ++frame)
        {
            SetShaderGraphTimeSeconds(2.0f);
            renderer.SetDetailGrass(fields);
            renderer.Render(projection * view.view, view, std::span(&ground, 1));
            const auto raw = readPixels(renderer.GetColorTexture());
            pixels.resize(raw.size());
            std::transform(raw.begin(), raw.end(), pixels.begin(), [](auto value) { return static_cast<std::uint8_t>(value); });
        }
        require(pixels.size() == std::size_t(Width) * Height * 4, "readback size");
        return pixels;
    };

    const auto bare = render(lighting, {});
    groundMask.resize(std::size_t(Width) * Height);
    std::size_t groundPixels = 0;
    for (std::size_t pixel = 0; pixel < groundMask.size(); ++pixel)
        groundPixels += (groundMask[pixel] = isGround(bare, pixel)) ? 1u : 0u;
    require(groundPixels > groundMask.size() / 4, "the ground should fill much of the view");
    const auto bareCoverage = coverage(bare);
    require(bareCoverage[0] < 0.01f && bareCoverage[1] < 0.01f, "the bare ground should not be green");

    const auto grown = render(lighting, std::span(&field, 1), 8);
    const auto grownCoverage = coverage(grown);
    std::cout << "Detail grass coverage: painted " << grownCoverage[0] << ", unpainted " << grownCoverage[1] << "\n";
    require(grownCoverage[0] > 0.3f, "grass should cover much of the painted half (" + std::to_string(grownCoverage[0]) + ")");
    require(grownCoverage[1] < 0.25f * grownCoverage[0], "grass should not cover the unpainted half like the painted one");
    require(renderer.GetFrameStats().detailGrassFields == 1 && renderer.GetFrameStats().detailGrassCandidates > 0,
            "the field should be generated");

    // Deterministic: the same view renders the same field, and generates the same number of clumps.
    const auto again = render(lighting, std::span(&field, 1), 4);
    require(again == grown, "an unchanged view must render the same grass");
    const auto stats = renderer.GetFrameStats().detailGrass;
    if (stats.available)
    {
        std::cout << "Detail grass clumps: " << stats.clumps[0] << " / " << stats.clumps[1] << " / " << stats.clumps[2] << "\n";
        require(stats.Total() > 100, "the generation pass should emit clumps (" + std::to_string(stats.Total()) + ")");
        render(lighting, std::span(&field, 1), 4);
        require(renderer.GetFrameStats().detailGrass.Total() == stats.Total(), "an unchanged view must generate the same clumps");
    }

    // Calm air: enabling the wind changes nothing.
    auto calm = field;
    calm.layer.wind.enabled = true;
    require(render(lighting, std::span(&calm, 1), 4) == grown, "calm air must render exactly as grass without wind");

    // A disabled layer draws nothing.
    auto disabled = field;
    disabled.layer.enabled = false;
    require(render(lighting, std::span(&disabled, 1), 3) == bare, "a disabled layer must draw nothing");

    // A small step keeps the field: clumps are anchored to the world.
    auto moved = lighting;
    moved.cameraPosition += glm::vec3(0.35f, 0.0f, -0.2f);
    moved.view = glm::lookAtRH(moved.cameraPosition, glm::vec3(0.35f, 0.0f, -2.2f), glm::vec3(0, 1, 0));
    const auto movedCoverage = coverage(render(moved, std::span(&field, 1), 4));
    require(std::abs(movedCoverage[0] - grownCoverage[0]) < 0.15f, "a small camera step should keep the painted half covered");

    require(renderer.Resize(previousWidth, previousHeight), "viewport restore failed");
}
