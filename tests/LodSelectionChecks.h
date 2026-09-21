#pragma once
#include "PlutoGE/render/Mesh.h"
#include <stdexcept>

inline void CheckLodSelection()
{
    using namespace PlutoGE::render;
    MeshConfig config;
    config.data.vertices.resize(3);
    config.data.indices = {0, 1, 2};
    config.submeshes = {{.indexOffset = 0, .indexCount = 3}};
    // Compare with the former two-pass selection algorithm, including invalid
    // imported ranges, clamps and exact transition endpoints.
    for (int variant = 0; variant < 4; ++variant)
    {
        config.submeshes[0].lods = {{0, 3, 0, 1000}, {0, 3, 0, 100}, {0, 3, 0, 25}};
        if (variant == 1) config.submeshes[0].lods[1].indexCount = 0;
        if (variant == 2) config.submeshes[0].lods[1].indexOffset = 4;
        if (variant == 3) config.submeshes[0].lods[1].maxScreenRadiusPixels = -1;
        Mesh mesh(config);
        for (uint32_t minimum : {0u, 1u, 2u, 255u})
            for (float radius : {0.f, 10.f, 21.25f, 24.f, 25.f, 28.75f, 50.f, 85.f, 99.f, 100.f, 115.f, 200.f, 2000.f})
            {
                const auto minIndex = std::min(minimum, uint32_t(mesh.GetSubmeshLodCount(0) - 1));
                const auto selected = std::max(uint32_t(mesh.SelectSubmeshLodByProjectedRadius(0, radius)), minIndex);
                uint32_t base = selected, transition = selected;
                float fade = 0;
                for (uint32_t far = 1; far < mesh.GetSubmeshLodCount(0); ++far)
                {
                    if (far - 1 < minIndex) continue;
                    const auto threshold = mesh.GetSubmeshLodRange(0, far).maxScreenRadiusPixels;
                    if (!std::isfinite(threshold) || threshold <= 0) continue;
                    const float upper = threshold * (1.f + .15f), lower = threshold * (1.f - .15f);
                    if (radius <= upper && radius >= lower)
                    {
                        base = far - 1;
                        transition = far;
                        fade = glm::clamp((upper - radius) / std::max(upper - lower, .001f), 0.f, 1.f);
                        break;
                    }
                }
                const auto actual = mesh.SelectSubmeshLodWithTransition(0, radius, minimum);
                if (actual.index != (fade > 0 && fade < 1 ? base : selected) ||
                    actual.transitionIndex != transition || actual.fade != fade)
                    throw std::runtime_error("Combined LOD selection changed a threshold or transition");
            }
    }
}
