#include "PlutoGE/render/FogTerrainShadow.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    std::vector<float> ComputeFogTerrainShadowHeights(std::span<const float> heights, int resolution, float cellSize,
                                                      const glm::vec3 &directionToSun)
    {
        const std::size_t size = static_cast<std::size_t>(std::max(resolution, 0));
        if (size == 0 || heights.size() != size * size || !(cellSize > 0.0f))
            return {};
        std::vector<float> shadow(heights.begin(), heights.end());

        const glm::vec2 horizontal(directionToSun.x, directionToSun.z);
        const float horizontalLength = glm::length(horizontal);
        // Below the horizon there is no sun to occlude; straight overhead nothing casts sideways.
        if (!(directionToSun.y > 0.0f) || horizontalLength < 1.0e-4f)
            return shadow;
        const glm::vec2 toSun = horizontal / horizontalLength;
        // The sun's rise per texel walked toward it.
        const float slope = directionToSun.y / horizontalLength * cellSize;

        const int last = resolution - 1;
        // A grid's value `steps` texels sunward of (x, z), filtered: bilinear inside the terrain, the highest
        // corner at its edge (so the shadow does not fade into the "no terrain" value). False off the terrain.
        const auto sample = [&](std::span<const float> grid, int x, int z, float steps, float &value)
        {
            const float ux = static_cast<float>(x) + toSun.x * steps;
            const float uz = static_cast<float>(z) + toSun.y * steps;
            const int x0 = static_cast<int>(std::floor(ux)), z0 = static_cast<int>(std::floor(uz));
            const float fx = ux - static_cast<float>(x0), fz = uz - static_cast<float>(z0);
            const float weights[4] = {(1.0f - fx) * (1.0f - fz), fx * (1.0f - fz), (1.0f - fx) * fz, fx * fz};
            const int cornersX[4] = {x0, x0 + 1, x0, x0 + 1}, cornersZ[4] = {z0, z0, z0 + 1, z0 + 1};
            float blended = 0.0f, highest = kFogTerrainNoHeight;
            bool allValid = true, any = false;
            for (int corner = 0; corner < 4; ++corner)
            {
                if (weights[corner] <= 1.0e-6f)
                    continue;
                const int cx = cornersX[corner], cz = cornersZ[corner];
                const float cornerValue = cx < 0 || cz < 0 || cx > last || cz > last
                    ? kFogTerrainNoHeight
                    : grid[static_cast<std::size_t>(cz) * size + static_cast<std::size_t>(cx)];
                if (cornerValue <= kFogTerrainNoHeight)
                {
                    allValid = false;
                    continue;
                }
                any = true;
                blended += weights[corner] * cornerValue;
                highest = std::max(highest, cornerValue);
            }
            value = allValid ? blended : highest;
            return any;
        };

        // One sweep from the sun's side. Each texel takes the highest of its own height, the terrain one texel
        // sunward and the shadow height two texels sunward, each lowered by the sun's rise over the distance. The
        // two-texel read lies strictly sunward in scan order (a one-texel one could read texels not swept yet),
        // and the one-texel terrain read keeps ridges a texel thin from slipping between the two-texel steps.
        const bool xDescending = toSun.x > 0.0f, zDescending = toSun.y > 0.0f;
        for (int zi = 0; zi < resolution; ++zi)
        {
            const int z = zDescending ? last - zi : zi;
            for (int xi = 0; xi < resolution; ++xi)
            {
                const int x = xDescending ? last - xi : xi;
                float &texel = shadow[static_cast<std::size_t>(z) * size + static_cast<std::size_t>(x)];
                float value = 0.0f;
                if (sample(heights, x, z, 1.0f, value))
                    texel = std::max(texel, value - slope);
                if (sample(shadow, x, z, 2.0f, value))
                    texel = std::max(texel, value - 2.0f * slope);
                texel = std::max(texel, kFogTerrainNoHeight);
            }
        }
        return shadow;
    }
}
