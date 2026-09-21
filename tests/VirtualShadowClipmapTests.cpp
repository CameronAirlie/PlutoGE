#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace PlutoGE::render;
void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main()
{
    try
    {
        BasicLighting lighting;
        lighting.directionalDirection = {0, 0, 1}; lighting.cameraPosition = {.1f, .1f, .1f};
        const auto original = VirtualShadowMaps::BuildClipmaps(lighting);
        const auto reduced = VirtualShadowMaps::BuildClipmaps(lighting, &original, 4.0f);
        Require(reduced.metrics[0].x == original.metrics[0].x * 4.0f &&
                reduced.origins[0].z != original.origins[0].z,
                "Resolution adaptation did not change fine-page identity");
        Require(std::memcmp(&reduced.matrices[PLUTO_VSM_ROOT_LEVEL], &original.matrices[PLUTO_VSM_ROOT_LEVEL], sizeof(glm::mat4)) == 0 &&
                reduced.origins[PLUTO_VSM_ROOT_LEVEL] == original.origins[PLUTO_VSM_ROOT_LEVEL],
                "Resolution adaptation discarded resident coarse coverage");
        Require(original.metrics[PLUTO_VSM_ROOT_LEVEL].x > original.metrics[PLUTO_VSM_FINE_LEVELS - 1].x,
                "Coarse VSM coverage must have a bounded low-resolution footprint");
        auto depthLighting = lighting;
        depthLighting.cameraPosition.z = 80.0f;
        const auto nearBoundary = VirtualShadowMaps::BuildClipmaps(depthLighting, &original);
        Require(nearBoundary.origins[0].z == original.origins[0].z,
                "Crossing a depth quantisation boundary discarded usable shadow depth");
        depthLighting.cameraPosition.z = 400.0f;
        const auto farBoundary = VirtualShadowMaps::BuildClipmaps(depthLighting, &nearBoundary);
        Require(farBoundary.origins[0].z != original.origins[0].z,
                "Depth hysteresis failed to recenter outside the safe envelope");
        lighting.view = glm::rotate(glm::mat4(1), .7f, glm::vec3(0, 1, 0));
        lighting.cameraPosition.x += .0001f;
        const auto moved = VirtualShadowMaps::BuildClipmaps(lighting);
        Require(std::memcmp(original.matrices.data(), moved.matrices.data(), sizeof(original.matrices)) == 0,
                "Camera orientation or sub-page motion destabilized clipmaps");
        lighting.cameraPosition.x += original.metrics[0].z;
        const auto scrolled = VirtualShadowMaps::BuildClipmaps(lighting);
        for (int level = 0; level < PLUTO_VSM_LEVELS; ++level)
        {
            Require(original.origins[level].z == scrolled.origins[level].z, "XY scroll changed projection epoch");
            const glm::vec4 point(.7f, .2f, .5f, 1);
            const auto before = (glm::vec2(original.matrices[level] * point) * .5f + .5f) * float(PLUTO_VSM_LEVEL_GRID(level)) + glm::vec2(original.origins[level]);
            const auto after = (glm::vec2(scrolled.matrices[level] * point) * .5f + .5f) * float(PLUTO_VSM_LEVEL_GRID(level)) + glm::vec2(scrolled.origins[level]);
            Require(glm::length(before - after) < .0001f, "Clipmap scroll changed absolute page addressing");
        }
        lighting.directionalDirection.x += .1f;
        const auto sun = VirtualShadowMaps::BuildClipmaps(lighting);
        Require(sun.origins[0].z != original.origins[0].z, "Sun rotation did not invalidate projection epoch");
        lighting.spotLights = {{{{1,2,3}, 10, {1,1,1}, 1, true}, {0,0,-1}}};
        const auto spot = VirtualShadowMaps::BuildClipmaps(lighting);
        constexpr int spotLevel = PLUTO_VSM_DIRECTIONAL_LEVELS;
        auto projected = spot.matrices[spotLevel] * glm::vec4(1,2,1,1);
        projected /= projected.w;
        Require(std::abs(projected.x) < .0001f && std::abs(projected.y) < .0001f && projected.z > 0 && projected.z < 1,
                "Spot projection does not cover the cone axis");
        lighting.cameraPosition += glm::vec3(10);
        const auto spotCameraMoved = VirtualShadowMaps::BuildClipmaps(lighting);
        Require(spot.origins[spotLevel] == spotCameraMoved.origins[spotLevel], "Camera movement invalidated spotlight pages");
        lighting.spotLights[0].light.position.x += 1;
        const auto spotMoved = VirtualShadowMaps::BuildClipmaps(lighting);
        Require(spot.origins[spotLevel].z != spotMoved.origins[spotLevel].z, "Spot movement retained stale projection epoch");
        lighting.spotLights[0].direction = {1,0,0};
        const auto spotRotated = VirtualShadowMaps::BuildClipmaps(lighting);
        Require(spotMoved.origins[spotLevel].z != spotRotated.origins[spotLevel].z, "Spot rotation retained stale projection epoch");
        lighting.spotLights[0].light.castsShadows = false;
        Require(VirtualShadowMaps::BuildClipmaps(lighting).origins[spotLevel].w == 0, "Disabled spotlight still requests pages");
        std::cout << "Virtual shadow clipmap policy passed\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
