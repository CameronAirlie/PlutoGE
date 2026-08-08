#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/surfacecache/SurfaceCache.h"
#include "PlutoGE/render/surfacecache/SurfaceCardSpatialIndex.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
    bool Overlaps(const PlutoGE::render::SurfaceCacheRect &a, const PlutoGE::render::SurfaceCacheRect &b)
    {
        return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
    }
}

int main()
{
    using namespace PlutoGE::render;
    static_assert(sizeof(SurfaceCardGpuBounds) == sizeof(float) * 32,
                  "Surface-card GPU bounds must match the std430 shader record.");
    auto effect = CreatePostProcessEffect("SurfaceCacheGI");
    if (!effect || effect->GetTypeName() != "SurfaceCacheGI")
    {
        std::cerr << "Surface cache GI is not registered with the effect factory.\n";
        return 8;
    }
    const auto parameters = effect->GetParameters();
    const auto radianceDebug = std::find_if(parameters.begin(), parameters.end(), [](const PostProcessParameter &parameter) {
        return parameter.name == "Debug View" && parameter.enumOptions.size() == 15 && parameter.enumOptions.back() == "Ray Hit Radiance";
    });
    if (radianceDebug == parameters.end())
    {
        std::cerr << "Surface cache GI does not expose the direct-radiance debug view.\n";
        return 9;
    }
    SurfaceCacheAtlasAllocator allocator(128, 128, 2);
    SurfaceCardSpatialIndex spatialIndex(2.0f);
    glm::mat4 cardProjection(1.0f);
    cardProjection[3][0] = 0.25f;
    spatialIndex.Rebuild({
        {1, {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, cardProjection, {0.25f, 0.5f, 0.1f, 0.2f}},
        {2, {0.5f, -0.5f, -0.5f}, {3.0f, 0.5f, 0.5f}},
    });
    const auto overlappingCards = spatialIndex.Query({0.75f, 0.0f, 0.0f});
    if (overlappingCards != std::vector<SurfaceCardId>({1, 2}) || !spatialIndex.Query({10.0f, 0.0f, 0.0f}).empty())
    {
        std::cerr << "Surface-card spatial lookup returned incorrect candidates.\n";
        return 10;
    }
    const auto gpuTables = spatialIndex.BuildGpuTables();
    if (gpuTables.cells.empty() || gpuTables.cards.size() != 2 || gpuTables.candidates.size() < 2 ||
        gpuTables.cards[0].minimumAndId.w != 1.0f || gpuTables.cards[1].minimumAndId.w != 2.0f ||
        gpuTables.cards[0].worldToCardClip[3][0] != 0.25f || gpuTables.cards[0].atlasScaleBias != glm::vec4(0.25f, 0.5f, 0.1f, 0.2f))
    {
        std::cerr << "Surface-card GPU lookup tables are incomplete or non-deterministic.\n";
        return 11;
    }
    std::vector<SurfaceCacheRect> allocations;
    for (int index = 0; index < 12; ++index)
    {
        auto allocation = allocator.Allocate(16, 12);
        if (!allocation)
        {
            std::cerr << "Atlas allocator unexpectedly ran out of room.\n";
            return 1;
        }
        for (const auto &existing : allocations)
            if (Overlaps(*allocation, existing))
            {
                std::cerr << "Atlas allocations overlap.\n";
                return 2;
            }
        if (allocation->x < 2 || allocation->y < 2)
        {
            std::cerr << "Atlas allocation did not preserve padding.\n";
            return 3;
        }
        allocations.push_back(*allocation);
    }
    if (allocator.Allocate(256, 16))
    {
        std::cerr << "Oversized allocation should have failed.\n";
        return 4;
    }
    allocator.Reset();
    if (allocator.GetUsedPixels() != 0)
    {
        std::cerr << "Allocator reset retained usage.\n";
        return 5;
    }

    MeshConfig meshConfig;
    meshConfig.data.vertices = {
        {{{-1,-2,-3}}, {{0,0,1}}, {{0,0}}, {{1,0,0,1}}},
        {{{ 1,-2,-3}}, {{0,0,1}}, {{1,0}}, {{1,0,0,1}}},
        {{{ 1, 2, 3}}, {{0,0,1}}, {{1,1}}, {{1,0,0,1}}},
        {{{-1, 2, 3}}, {{0,0,1}}, {{0,1}}, {{1,0,0,1}}},
    };
    meshConfig.data.indices = {0,1,2,2,3,0};
    Mesh mesh(meshConfig);
    const auto cards = SurfaceCardGenerator::GenerateAxisCards(mesh, 0, 16, 16, 256);
    if (cards.size() != 6)
    {
        std::cerr << "Axis generator did not produce six cards.\n";
        return 6;
    }
    for (const auto &card : cards)
    {
        if (!std::isfinite(card.localViewProjection[0][0]) || card.allocation.width < 16 || card.allocation.height < 16)
        {
            std::cerr << "Generated card contains invalid projection or resolution.\n";
            return 7;
        }
    }
    return 0;
}
