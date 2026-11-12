#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <glm/glm.hpp>

#include <vector>

namespace PlutoGE::render
{
    // How far a particle's streak reaches behind it: the distance it moved over the last `stretch` seconds.
    inline float ParticleStreakLength(const glm::vec3 &velocity, float stretch)
    {
        return stretch > 0.0f ? glm::length(velocity) * stretch : 0.0f;
    }

    // Appends a particle drawn as a streak along its motion (rain, sparks): a quad from where the particle was
    // `stretch` seconds ago to where it is, turned to face the camera about that line and as wide as the particle.
    // It is never shorter than it is wide, so a particle coming straight at the camera, or standing still, still
    // shows as a square. u runs across the streak, v from its tail (0) to its head (1).
    inline void AppendParticleStreak(std::vector<BasicParticleVertex> &vertices, const glm::vec3 &position,
                                     const glm::vec3 &velocity, float stretch, float width,
                                     const glm::vec3 &cameraPosition, const glm::vec3 &cameraRight,
                                     const glm::vec3 &cameraUp, const glm::vec4 &color, const glm::vec4 &data)
    {
        const float speed = glm::length(velocity);
        glm::vec3 along = speed > 1e-5f ? velocity / speed : cameraUp;
        const glm::vec3 toCamera = cameraPosition - position;
        glm::vec3 side = glm::cross(along, toCamera);
        if (glm::dot(side, side) <= 1e-10f)
        {
            // Moving along the line of sight: a square billboard.
            along = cameraUp;
            side = cameraRight;
        }
        side = glm::normalize(side) * (width * 0.5f);
        const float length = glm::max(ParticleStreakLength(velocity, stretch), width);
        const glm::vec3 head = position;
        const glm::vec3 tail = position - along * length;
        const glm::vec3 positions[] = {tail - side, tail + side, head - side, head - side, tail + side, head + side};
        constexpr glm::vec2 uvs[] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
        for (int vertex = 0; vertex < 6; ++vertex)
            vertices.push_back({positions[vertex], color, uvs[vertex], data, position});
    }
}
