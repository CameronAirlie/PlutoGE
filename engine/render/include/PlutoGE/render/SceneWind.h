#pragma once
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    // The scene's wind field, authored on a WindComponent and carried on BasicLighting. It is the one wind setting
    // the scene's consumers share: foliage bends with it today, and any CPU system (particles, cloth, audio) can
    // sample the same field with SceneWindVelocity. Calm (speed 0) by default, which leaves the renderer unchanged.
    struct SceneWindSettings
    {
        glm::vec2 direction{1.0f, 0.0f}; // unit world XZ direction the wind blows toward
        float speed = 0.0f;              // metres per second
        float gustiness = 0.4f;          // 0 steady, 1 gusts swing between calm and twice the mean strength
        float gustSize = 40.0f;          // metres between gust fronts, which travel downwind at the wind speed
        // Virtual shadow pages within this distance of the camera (metres) animate swaying casters; pages beyond it
        // hold their rest pose, so they stay cached instead of being redrawn every frame. 0 sways shadows at every
        // distance.
        float shadowSwayDistance = 0.0f;

        bool Calm() const { return !(speed > 0.0f); }
        bool operator==(const SceneWindSettings &) const = default;
    };

    // Unit XZ direction from a compass angle in degrees: 0 blows toward +X, 90 toward +Z.
    glm::vec2 SceneWindDirectionFromDegrees(float degrees);
    // Speed over PLUTO_WIND_REFERENCE_SPEED, clamped to [0, PLUTO_WIND_MAX_STRENGTH].
    float SceneWindStrength(const SceneWindSettings &wind);
    // Gust multiplier of the mean strength at a world XZ position, in [1 - gustiness, 1 + gustiness]. FoliageWind.slang
    // evaluates the same function on the GPU, so a CPU consumer sees the gust a plant beside it shows.
    float SceneWindGust(const SceneWindSettings &wind, glm::vec2 positionXZ, float time);
    // Wind velocity (m/s, world space) at a position and time, gusts included.
    glm::vec3 SceneWindVelocity(const SceneWindSettings &wind, const glm::vec3 &position, float time);
}
