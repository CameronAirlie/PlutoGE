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
        Require(VirtualShadowPoolTiles(256) == 16 && VirtualShadowPoolTiles(300) == 24 && VirtualShadowPoolTiles(1024) == 32,
                "Physical pool tiers are inconsistent");
        VirtualShadowResolutionPolicy policy;
        Require(policy.Observe(1, 1, 300, 192, 192) && policy.Scale() == 2, "Pressure must reduce resolution");
        Require(!policy.Observe(2, 2, 0, 0, 192), "Stale feedback bypassed settle period");
        for (std::uint32_t frame = 33; frame < 65; ++frame) policy.Observe(frame, frame, 20, 20, 128);
        Require(policy.Scale() == 1, "Local light reservations blocked directional resolution recovery");
        Require(!policy.Observe(66, 66, 20, 20, 128), "Stable residency unnecessarily degraded resolution");
        VirtualShadowResolutionPolicy hysteresis;
        hysteresis.Observe(1, 1, 300, 192, 192);
        for (std::uint32_t frame = 33; frame < 200; ++frame) hysteresis.Observe(frame, frame, 80, 80, 192);
        Require(hysteresis.Scale() == 2, "Refinement ignored predicted fourfold demand");
        std::vector<BasicVertex> clusterVertices(6);
        clusterVertices[0].position = {-1,-1,0}; clusterVertices[1].position = {1,-1,0}; clusterVertices[2].position = {0,1,0};
        clusterVertices[3].position = {99,-1,0}; clusterVertices[4].position = {101,-1,0}; clusterVertices[5].position = {100,1,0};
        std::vector<std::uint32_t> clusterIndices;
        for (int i = 0; i < ShadowClusterTriangleCount; ++i) clusterIndices.insert(clusterIndices.end(), {0,1,2});
        for (int i = 0; i < ShadowClusterTriangleCount; ++i) clusterIndices.insert(clusterIndices.end(), {3,4,5});
        const auto clusters = BuildShadowGeometryClusters(std::span<const BasicVertex>(clusterVertices), std::span<const std::uint32_t>(clusterIndices));
        Require(clusters.size() == 2 && clusters[0].firstIndex == 0 && clusters[1].firstIndex == ShadowClusterTriangleCount * 3 && clusters[1].indexCount == ShadowClusterTriangleCount * 3,
                "Shadow clusters dropped or overlapped triangles");
        Require(clusters[0].center.x == 0 && clusters[1].center.x == 100 && clusters[0].extents.x == 1,
                "Spatially separate index ranges retained whole-mesh bounds");
        const auto submeshClusters = SelectShadowGeometryClusters(clusters, ShadowClusterTriangleCount * 3 + 2, 3);
        Require(submeshClusters.size() == 1 && submeshClusters.front().firstIndex == ShadowClusterTriangleCount * 3,
                "Submesh selection included unrelated geometry clusters");
        auto transform = glm::scale(glm::mat4(1), glm::vec3(-2,3,1)); transform[1].x = 1.5f;
        const auto sphere = ShadowClusterWorldSphere(clusters[0], std::span<const glm::mat4>(&transform, 1));
        for (int i = 0; i < 3; ++i)
            Require(glm::length(glm::vec3(transform * glm::vec4(clusterVertices[i].position[0], clusterVertices[i].position[1], clusterVertices[i].position[2], 1)) - glm::vec3(sphere)) <= sphere.w,
                    "Cluster bounds lost geometry under mirrored scale/shear");
        clusterVertices[0].position[0] = -20;
        const auto deformed = BuildShadowGeometryClusters(std::span<const BasicVertex>(clusterVertices), std::span<const std::uint32_t>(clusterIndices));
        Require(deformed[0].extents.x > clusters[0].extents.x, "Deformation retained stale cluster bounds");
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
        lighting.spotLights[0].cone.outerAngle = 90;
        const auto wideSpot = VirtualShadowMaps::BuildClipmaps(lighting);
        Require(wideSpot.origins[spotLevel].z != spot.origins[spotLevel].z, "Spot cone edit did not invalidate VSM projection");
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
