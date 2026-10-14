#pragma once
#include <algorithm>
#include <limits>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    // Unknown cells retain the complete finite-plane visibility evaluation.
    struct ProbeVisibilitySummary { glm::vec4 plane, bounds; };

    inline int FoldProbeVisibilityTexel(int x, int y)
    {
        if (x < 0 || x >= 8) { x = x < 0 ? -x-1 : 15-x; y = 7-y; }
        if (y < 0 || y >= 8) { y = y < 0 ? -y-1 : 15-y; x = 7-x; }
        return x + y * 8;
    }

    template<class Surface, class Hit>
    ProbeVisibilitySummary SummarizeProbeVisibilityCell(int x, int y, Surface surface, Hit hit)
    {
        const auto first = surface(FoldProbeVisibilityTexel(x,y));
        const glm::vec3 normal(first.y,first.z,first.w);
        float minDepth = std::numeric_limits<float>::max(), maxDepth = 0;
        float minPlane = std::numeric_limits<float>::max(), maxPlane = -minPlane;
        bool sameNormal = true, noPlanes = true;
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
        {
            const int index = FoldProbeVisibilityTexel(x+dx,y+dy);
            const auto sample = surface(index);
            const glm::vec3 n(sample.y,sample.z,sample.w);
            sameNormal &= glm::all(glm::equal(n,normal));
            noPlanes &= glm::dot(n,n) < .49f;
            minDepth = std::min(minDepth,sample.x); maxDepth = std::max(maxDepth,sample.x);
            minPlane = std::min(minPlane,hit(index).w); maxPlane = std::max(maxPlane,hit(index).w);
        }
        const int mode = noPlanes ? 0 : sameNormal && glm::dot(normal,normal) > .51f ? 1 : -1;
        return {glm::vec4(normal,minPlane),glm::vec4(maxPlane,minDepth,mode,maxDepth)};
    }
}
