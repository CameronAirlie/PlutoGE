// FogFroxels.h: the froxel grid, its slice spacing, the per-frame jitter and when history may be reused.
#include "PlutoGE/render/FogFroxels.h"

#include <cmath>
#include <iostream>
#include <set>
#include <tuple>
#include <stdexcept>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void CheckGrid()
    {
        const FogFroxelSettings settings;
        const auto grid = FogFroxelGrid::For(1920, 1080, settings);
        Require(grid.width == 120 && grid.height == 68 && grid.slices == 64, "1080p takes 120 x 68 x 64 froxels");
        Require(grid.AtlasWidth() == 120 * 64 && grid.AtlasHeight() == 68, "The atlas lays the slices side by side");
        const auto tiny = FogFroxelGrid::For(1, 1, {.tilePixels = 0, .slices = 1000});
        Require(tiny.width == 1 && tiny.height == 1 && tiny.slices == FogFroxelGrid::kMaxSlices,
                "Degenerate settings still give a valid grid");
    }

    void CheckSlices()
    {
        Require(FogFroxelSliceDistance(0, 64, 80) == 0 && FogFroxelSliceDistance(64, 64, 80) == 80,
                "Slices span the detail distance");
        // Quadratic spacing: the near half of the slices covers the nearest quarter of the range.
        Require(std::abs(FogFroxelSliceDistance(32, 64, 80) - 20) < 1e-4f, "Slices are finer near the camera");
    }

    void CheckJitter()
    {
        std::set<std::tuple<float, float, float>> points;
        for (std::uint64_t frame = 0; frame < 16; ++frame)
        {
            const auto jitter = FogFroxelJitter(frame);
            Require(jitter.x >= 0 && jitter.x < 1 && jitter.y >= 0 && jitter.y < 1 && jitter.z >= 0 && jitter.z < 1,
                    "Jitter stays inside the froxel");
            points.emplace(jitter.x, jitter.y, jitter.z);
        }
        Require(points.size() == 16, "Sixteen frames light sixteen distinct points");
        Require(FogFroxelJitter(3) == FogFroxelJitter(19), "The jitter repeats every sixteen frames");
    }

    void CheckHistory()
    {
        const auto grid = FogFroxelGrid::For(640, 360, {});
        FogFroxelHistory::Appearance appearance{};
        appearance[0] = {1, 1, 1, .05f};
        FogFroxelLighting lighting{.sunDirection = {0, -1, 0}, .sunRadiance = {4, 4, 4}, .ambient = 1, .shadows = true};
        FogFroxelHistory history;
        Require(!history.Observe(grid, appearance, lighting), "The first frame has no history");
        Require(history.Observe(grid, appearance, lighting), "An unchanged frame reuses history");

        auto drifting = lighting;
        drifting.sunRadiance *= 1.02f;
        drifting.sunDirection = glm::normalize(glm::vec3(.01f, -1, 0));
        Require(history.Observe(grid, appearance, drifting), "Gradual lighting changes converge through history");
        auto switched = drifting;
        switched.sunRadiance *= .5f;
        Require(!history.Observe(grid, appearance, switched), "A light step discards history");
        auto toggled = switched;
        toggled.cloudShadows = true;
        Require(!history.Observe(grid, appearance, toggled), "Toggling cloud shadows discards history");

        Require(history.Observe(grid, appearance, toggled), "History returns once the state holds");
        auto denser = appearance;
        denser[0].w = .06f;
        Require(!history.Observe(grid, denser, toggled), "A fog parameter change discards history");
        Require(!history.Observe(FogFroxelGrid::For(1280, 720, {}), denser, toggled), "A new grid discards history");
        history.Invalidate();
        Require(!history.Observe(FogFroxelGrid::For(1280, 720, {}), denser, toggled), "Invalidate discards history");
    }
}

int main()
{
    try
    {
        CheckGrid();
        CheckSlices();
        CheckJitter();
        CheckHistory();
        std::cout << "Fog froxel tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
