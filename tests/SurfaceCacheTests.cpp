#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/surfacecache/SurfaceCache.h"
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
    auto effect = CreatePostProcessEffect("SurfaceCacheGI");
    if (!effect || effect->GetTypeName() != "SurfaceCacheGI")
    {
        std::cerr << "Surface cache GI is not registered with the effect factory.\n";
        return 8;
    }
    const auto parameters = effect->GetParameters();
    const auto radianceDebug = std::find_if(parameters.begin(), parameters.end(), [](const PostProcessParameter &parameter) {
        return parameter.name == "Debug View" && parameter.enumOptions.size() == 6 && parameter.enumOptions.back() == "Direct Radiance";
    });
    if (radianceDebug == parameters.end())
    {
        std::cerr << "Surface cache GI does not expose the direct-radiance debug view.\n";
        return 9;
    }
    SurfaceCacheAtlasAllocator allocator(128, 128, 2);
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
