#pragma once
#include "PlutoGE/render/Renderer.h"
#include <algorithm>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // Match the legacy VCT policy: authored static geometry forms the cache.
    // Entirely unclassified imported scenes retain rigid-mesh GI support.
    inline void SelectVctScene(std::span<const RenderCommand> source, std::vector<RenderCommand> &destination)
    {
        const auto rigid = [](const RenderCommand &command) {
            return command.mesh && command.material && (!command.jointMatrices || command.jointMatrices->empty());
        };
        const bool hasStatic = std::ranges::any_of(source, [&](const auto &command) { return command.isStatic && rigid(command); });
        destination.clear();
        destination.reserve(source.size());
        for (const auto &command : source)
            if (rigid(command) && (!hasStatic || command.isStatic)) destination.push_back(command);
    }
}
