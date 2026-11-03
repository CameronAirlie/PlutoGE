#pragma once
#include "ImpostorDraws.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/RenderDevice.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

// Impostor cards (ImpostorDraws.h, BasicLitImpostor.slang): a mesh draw hands instances smaller than its impostor's
// threshold to the impostor draw MakeImpostorDraw derives, so each instance draws exactly once, as its mesh or as a
// two-triangle card, and the cards cover pixels. The expansion keeps a source packet's impostor packet.
template <class ReadPixels>
inline void CheckImpostors(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                           ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    // The statistics checked here count CPU-binned instances (GpuInstanceCulling has its own check).
    renderer.SetGpuInstanceCulling(false);
    renderer.Resize(64, 64);
    // LOD0: a 10 x 10 grid of quads (200 triangles); LOD1: one quad.
    std::vector<BasicVertex> vertices;
    for (int y = 0; y <= 10; ++y)
        for (int x = 0; x <= 10; ++x)
            vertices.push_back({{{x * 0.1f - 0.5f, y * 0.1f - 0.5f, 0.0f}}, {{0, 0, 1}}, {{x * 0.1f, y * 0.1f}}});
    std::vector<std::uint32_t> indices;
    for (std::uint32_t y = 0; y < 10; ++y)
        for (std::uint32_t x = 0; x < 10; ++x)
        {
            const std::uint32_t a = y * 11 + x;
            indices.insert(indices.end(), {a, a + 1, a + 12, a, a + 12, a + 11});
        }
    const auto lod0Count = static_cast<std::uint32_t>(indices.size());
    indices.insert(indices.end(), {0, 10, 120, 0, 120, 110});
    auto mesh = renderer.CreateMesh({vertices, indices});

    // A 2 x 2 frame atlas of 8-pixel frames: a white opaque disc in each, facing its frame.
    constexpr std::uint32_t atlasSize = 16;
    std::vector<std::uint8_t> albedoPixels(atlasSize * atlasSize * 4), normalPixels(atlasSize * atlasSize * 4);
    for (std::uint32_t y = 0; y < atlasSize; ++y)
        for (std::uint32_t x = 0; x < atlasSize; ++x)
        {
            const float dx = static_cast<float>(x % 8) + 0.5f - 4.0f, dy = static_cast<float>(y % 8) + 0.5f - 4.0f;
            const bool inside = dx * dx + dy * dy < 9.0f;
            auto *albedo = &albedoPixels[(y * atlasSize + x) * 4];
            auto *normal = &normalPixels[(y * atlasSize + x) * 4];
            albedo[0] = albedo[1] = albedo[2] = 255;
            albedo[3] = inside ? 255 : 0;
            normal[0] = normal[1] = 128; normal[2] = 255; normal[3] = 128;
        }
    const auto texture = [&](const std::vector<std::uint8_t> &pixels, rhi::Format format, const char *name)
    {
        return rhi::Texture(device, device.CreateTexture({atlasSize, atlasSize, format, rhi::TextureUsage::Sampled, name},
                                                         std::as_bytes(std::span(pixels))));
    };
    const auto albedo = texture(albedoPixels, rhi::Format::R8G8B8A8Srgb, "Test impostor albedo");
    const auto normalDepth = texture(normalPixels, rhi::Format::R8G8B8A8Unorm, "Test impostor normal depth");
    auto impostor = std::make_shared<BasicImpostor>();
    impostor->parameters = {.framesPerSide = 2, .frameResolution = 8, .boundsCenter = glm::vec3(0.0f), .boundsRadius = 0.71f,
                            .maxScreenRadiusPixels = 8.0f};
    impostor->albedo = albedo.Get();
    impostor->normalDepth = normalDepth.Get();

    auto chain = std::make_shared<BasicInstanceLodChain>();
    chain->boundsRadius = 0.71f;
    chain->lods = {{0, lod0Count, 1e30f}, {lod0Count, 6, 1.0f}};
    chain->minScreenRadiusPixels = impostor->parameters.maxScreenRadiusPixels;

    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.cameraPosition = {0, 0, 0};
    lighting.view = glm::mat4(1);
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.f), 1.f, 1000.f, .1f);
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.baseColor = glm::vec4(1.0f);
    draw.castsShadow = false;
    draw.firstIndex = 0;
    draw.indexCount = lod0Count;
    draw.instanceLods = chain;
    draw.spawnsImpostor = impostor;
    const auto render = [&](std::shared_ptr<std::vector<glm::mat4>> models)
    {
        draw.instanceModels = models;
        const std::vector<BasicDraw> draws{draw, MakeImpostorDraw(draw)};
        renderer.Render(projection * lighting.view, lighting, draws);
        return renderer.GetFrameStats();
    };

    // 0.71 / 3 * 55 px is about 13 px: the mesh; at 6 m about 6.5 px: a card.
    auto mixed = std::make_shared<std::vector<glm::mat4>>();
    mixed->push_back(glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, -3.0f)));
    mixed->push_back(glm::translate(glm::mat4(1), glm::vec3(1.0f, 0.0f, -6.0f)));
    const auto stats = render(mixed);
    std::cout << "Impostors: " << stats.geometryTriangles[0] << " mesh and " << stats.geometryTriangles[1]
              << " card triangles\n";
    if (stats.geometryTriangles[0] != 200u || stats.geometryTriangles[1] != 2u)
        throw std::runtime_error("A near instance must draw its mesh and a far one its impostor card, each once");

    // Cards alone cover the pixels where they stand.
    auto far = std::make_shared<std::vector<glm::mat4>>();
    far->push_back(glm::translate(glm::mat4(1), glm::vec3(-1.0f, 0.0f, -6.0f)));
    far->push_back(glm::translate(glm::mat4(1), glm::vec3(1.0f, 0.0f, -6.0f)));
    render(far);
    const auto withCards = readPixels(renderer.GetColorTexture());
    renderer.Render(projection * lighting.view, lighting, std::span<const BasicDraw>{});
    const auto empty = readPixels(renderer.GetColorTexture());
    // x = 32 -+ 1 / 6 * 55 px.
    const auto changed = [&](int x, int y)
    {
        const std::size_t texel = (static_cast<std::size_t>(y) * 64 + x) * 4;
        return withCards[texel] != empty[texel] || withCards[texel + 1] != empty[texel + 1] || withCards[texel + 2] != empty[texel + 2];
    };
    if (!changed(23, 32) || !changed(41, 32))
        throw std::runtime_error("Impostor cards must cover the pixels of their instances");
    if (changed(32, 10))
        throw std::runtime_error("Impostor cards drew where no instance stands");
    // GPU instance culling draws the same cards through its bins.
    if (renderer.GpuInstanceCullingAvailable())
    {
        renderer.SetGpuInstanceCulling(true);
        render(far);
        const auto gpuCards = readPixels(renderer.GetColorTexture());
        renderer.SetGpuInstanceCulling(false);
        if (gpuCards != withCards)
            throw std::runtime_error("Impostor cards binned on the GPU must match CPU binning");
    }

    // The expansion appends one impostor draw per spawning draw and keeps it while its source packet is unchanged.
    ImpostorDraws expansion;
    draw.preparationRevision = 7;
    std::uint64_t revision = 100;
    const auto next = [&] { return ++revision; };
    const auto first = expansion.Expand(std::span(&draw, 1), true, next);
    const auto firstRevision = first.size() == 2 ? first[1].preparationRevision : 0;
    const auto again = expansion.Expand(std::span(&draw, 1), true, next);
    if (first.size() != 2 || !first[1].impostor || again.size() != 2 || again[1].preparationRevision != firstRevision)
        throw std::runtime_error("The impostor expansion must add one card draw and keep it for an unchanged source");
}
