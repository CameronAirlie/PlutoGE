#include "ParticleStreak.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; }

    struct Extent
    {
        glm::vec3 min{1e9f}, max{-1e9f};
    };

    Extent Bounds(const std::vector<BasicParticleVertex> &vertices)
    {
        Extent extent;
        for (const auto &vertex : vertices)
        {
            extent.min = glm::min(extent.min, vertex.position);
            extent.max = glm::max(extent.max, vertex.position);
        }
        return extent;
    }
}

int main()
try
{
    const glm::vec3 camera{0, 0, 10}, right{1, 0, 0}, up{0, 1, 0};
    const glm::vec4 color{0.8f, 0.85f, 0.9f, 0.5f}, data{0.2f, 1, 0.5f, 0.03f};

    // A falling drop seen from the side is a vertical streak, its head at the drop, its tail where it was.
    {
        std::vector<BasicParticleVertex> vertices;
        AppendParticleStreak(vertices, {0, 5, 0}, {0, -12, 0}, 0.05f, 0.03f, camera, right, up, color, data);
        Require(vertices.size() == 6, "A streak is one quad of two triangles");
        const auto extent = Bounds(vertices);
        Require(Near(extent.min.y, 5) && Near(extent.max.y, 5 + 0.6f), "The streak reaches back up to where it was");
        Require(Near(extent.max.x - extent.min.x, 0.03f), "The streak is as wide as the particle");
        Require(Near(extent.max.z, 0) && Near(extent.min.z, 0), "The streak faces the camera");
        for (const auto &vertex : vertices)
        {
            Require(vertex.color == color && vertex.ageDensityRandomSize == data, "Every vertex carries the particle");
            Require(vertex.center == glm::vec3(0, 5, 0), "The streak's centre is the particle");
            Require(Near(vertex.uv.y, vertex.position.y < 5.3f ? 1.0f : 0.0f), "v runs from tail to head");
        }
    }

    // A slow or still particle is never shorter than it is wide.
    {
        std::vector<BasicParticleVertex> vertices;
        AppendParticleStreak(vertices, {0, 0, 0}, {0, 0, 0}, 0.05f, 0.1f, camera, right, up, color, data);
        const auto extent = Bounds(vertices);
        Require(Near(extent.max.y - extent.min.y, 0.1f) && Near(extent.max.x - extent.min.x, 0.1f),
                "A still particle is a square");
    }

    // A particle coming straight at the camera does not collapse to a line.
    {
        std::vector<BasicParticleVertex> vertices;
        AppendParticleStreak(vertices, {0, 0, 0}, {0, 0, 20}, 0.05f, 0.1f, camera, right, up, color, data);
        const auto extent = Bounds(vertices);
        Require(extent.max.x - extent.min.x > 0.09f && extent.max.y - extent.min.y > 0.09f,
                "Motion along the line of sight still draws a square");
    }

    Require(Near(ParticleStreakLength({0, -10, 0}, 0.05f), 0.5f) && ParticleStreakLength({0, -10, 0}, 0) == 0,
            "The streak's reach is speed times stretch");

    std::cout << "ParticleStreakTests passed\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << "ParticleStreakTests failed: " << exception.what() << '\n';
    return 1;
}
