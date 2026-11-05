#pragma once

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    // Multiplies the distance at which geometry changes LOD: at 2, every switch happens twice as far from the camera.
    // Selection multiplies the projected radius by it, as BasicLighting::foliageDetail does for a whole quality tier, so
    // a scale moves a chain's thresholds, triangle budget, crossfades and impostor hand-over together. Triangles grow
    // with its square.
    inline constexpr float DefaultLodDistanceScale = 1.0f;
    inline constexpr float MinLodDistanceScale = 0.1f;
    inline constexpr float MaxLodDistanceScale = 10.0f;

    [[nodiscard]] inline float SanitizeLodDistanceScale(float scale)
    {
        return std::isfinite(scale) ? std::clamp(scale, MinLodDistanceScale, MaxLodDistanceScale) : DefaultLodDistanceScale;
    }
}
