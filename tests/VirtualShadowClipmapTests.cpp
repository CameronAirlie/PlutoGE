#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
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
        VirtualShadowBudgetPolicy budget;
        for (int repeat = 0; repeat < 32; ++repeat) budget.Observe(1, .1f, 10);
        Require(budget.Budget(1000000) == 1000000, "Repeated GPU observations grew the budget");
        for (std::uint64_t sample = 2; sample <= 8; ++sample) budget.Observe(sample, .1f, 10);
        Require(budget.Budget(1000000) == 1250000, "Sustained GPU headroom did not grow the budget");
        budget.Observe(9, .8f, 10);
        Require(budget.Budget(1000000) == 1000000, "GPU pressure did not back off to the authored budget");
        for (std::uint64_t sample = 10; sample < 200; ++sample) budget.Observe(sample, .1f, 10);
        Require(budget.Budget(1000000) == 4000000 && budget.Budget(8000000) == 16000000,
            "Adaptive budget exceeded its hard limits");
        budget.Observe(201, .1f, 20);
        Require(budget.Budget(1000000) == 3000000, "Scene GPU pressure did not reduce shadow work");
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
        const int root = static_cast<int>(original.levels.x);
        const auto reduced = VirtualShadowMaps::BuildClipmaps(lighting, &original, 4.0f);
        Require(reduced.metrics[0].x == original.metrics[0].x * 4.0f &&
                reduced.origins[0].z != original.origins[0].z,
                "Resolution adaptation did not change fine-page identity");
        Require(std::memcmp(&reduced.matrices[root], &original.matrices[root], sizeof(glm::mat4)) == 0 &&
                reduced.origins[root] == original.origins[root],
                "Resolution adaptation discarded resident coarse coverage");
        Require(original.metrics[root].x > original.metrics[root - 1].x,
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
            const auto before = (glm::vec2(original.matrices[level] * point) * .5f + .5f) * float(PLUTO_VSM_LEVEL_GRID(level, root)) + glm::vec2(original.origins[level]);
            const auto after = (glm::vec2(scrolled.matrices[level] * point) * .5f + .5f) * float(PLUTO_VSM_LEVEL_GRID(level, root)) + glm::vec2(scrolled.origins[level]);
            Require(glm::length(before - after) < .0001f, "Clipmap scroll changed absolute page addressing");
        }
        // Longer shadow distances add fine levels instead of coarsening the finest; up to 256 m keep four.
        Require(VirtualShadowFineLevels(8.0f) == PLUTO_VSM_MIN_FINE_LEVELS && VirtualShadowFineLevels(142.0f) == 4 &&
                VirtualShadowFineLevels(256.0f) == 4 && VirtualShadowFineLevels(300.0f) == 5 &&
                VirtualShadowFineLevels(1000.0f) == 6 && VirtualShadowFineLevels(1.0e6f) == PLUTO_VSM_MAX_FINE_LEVELS,
                "Fine clipmap level count did not follow the shadow distance");
        {
            auto farLighting = lighting;
            farLighting.shadowDistance = 600.0f;
            const auto far = VirtualShadowMaps::BuildClipmaps(farLighting);
            const int farRoot = static_cast<int>(far.levels.x);
            Require(farRoot == 6 && far.metrics[farRoot - 1].w == 1200.0f && far.metrics[0].w <= PLUTO_VSM_MAX_FINEST_SPAN &&
                    far.metrics[farRoot].w == far.metrics[farRoot - 1].w,
                    "A long shadow distance must span it with the outer fine level and keep the finest one small");
            for (int level = 1; level < farRoot; ++level)
                Require(far.metrics[level].w == 2.0f * far.metrics[level - 1].w, "Fine clipmap levels must double in span");
            auto nearLighting = lighting;
            nearLighting.shadowDistance = 142.0f;
            const auto near = VirtualShadowMaps::BuildClipmaps(nearLighting);
            Require(near.levels.x == 4 && near.metrics[0].w == 35.5f && near.metrics[3].w == 284.0f,
                    "Distances up to 256 m must keep the four-level layout");
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

        // Chunking steps down one level at a time as the chunks a frame needs exceed the capacity.
        Require(SelectVirtualShadowChunking({100, 50, 10}, true, 4096) == VirtualShadowChunking::ClusteredLodVariants,
                "LOD variants were not used within capacity");
        Require(SelectVirtualShadowChunking({5000, 4000, 2000}, true, 4096) == VirtualShadowChunking::Clustered,
                "Clusters were dropped while they still fitted");
        Require(SelectVirtualShadowChunking({5000, 5000, 4000}, true, 4096) == VirtualShadowChunking::MaskedModels,
                "Over-capacity clusters fell back past masked models");
        Require(SelectVirtualShadowChunking({5000, 5000, 5000}, true, 4096) == VirtualShadowChunking::WholeDraws,
                "Masked models were used beyond capacity");
        Require(SelectVirtualShadowChunking({10, 10, 10}, false, 4096) == VirtualShadowChunking::MaskedModels,
                "Mesh clusters were used with cluster culling off");
        // Caster slots follow the chunks needed, with headroom, never fewer than needed nor more than the limit.
        Require(VirtualShadowCasterCapacity(0) == 512 && VirtualShadowCasterCapacity(5120) == 6144,
                "Caster capacity does not follow the chunks needed");
        Require(VirtualShadowCasterCapacity(8000) == PLUTO_VSM_MAX_DRAW_CHUNKS &&
                VirtualShadowCasterCapacity(PLUTO_VSM_MAX_DRAW_CHUNKS) == PLUTO_VSM_MAX_DRAW_CHUNKS,
                "Caster capacity exceeded the chunk limit");

        // Caster LOD variants: a 10 m-wide caster drawn at LOD 2, under Verdant's five fine levels and the root.
        ShadowCasterLodChain chain;
        chain.boundsRadius = 1.0f;
        for (const float triangles : {60000.0f, 15000.0f, 5000.0f, 1500.0f, 500.0f}) chain.lods.push_back({0, 0, triangles});
        std::array<float, PLUTO_VSM_LEVELS> texels{};
        const std::array<float, 6> verdantTexels{0.003f, 0.006f, 0.012f, 0.024f, 0.049f, 0.78f};
        std::copy(verdantTexels.begin(), verdantTexels.end(), texels.begin());
        const auto variants = SelectShadowCasterLodVariants(chain, 2, 5.0f, texels);
        std::uint32_t covered = 0;
        bool disjoint = true;
        for (const auto &variant : variants)
        {
            disjoint &= (covered & variant.levels) == 0;
            covered |= variant.levels;
            Require(variant.lod >= 2, "A level drew a caster finer than its own LOD");
        }
        Require(disjoint && covered == PLUTO_VSM_ALL_LEVELS, "LOD variants do not partition the levels");
        Require(variants.size() == 3 && variants[0].lod == 2 && variants[1].lod == 3 && variants[2].lod == 4 &&
                variants[1].levels == (1u << 4) && variants[2].levels == (1u << 5),
                "Coarse levels did not take the LODs their texels resolve");
        Require((variants[0].levels & (1u << PLUTO_VSM_DIRECTIONAL_LEVELS)) != 0,
                "A level without a texel size did not keep the caster's own LOD");
        // A LOD saving too little over the next finer one kept is not worth its chunks: its levels draw the finer one.
        chain.lods[3].meshTriangles = 4000.0f;
        const auto merged = SelectShadowCasterLodVariants(chain, 2, 5.0f, texels);
        Require(merged.size() == 2 && merged[0].lod == 2 && (merged[0].levels & (1u << 4)) != 0 && merged[1].lod == 4,
                "A LOD saving little over the next finer one got its own variant");
        Require(SelectShadowCasterLodVariants(chain, 2, -1.0f, texels).size() == 1 &&
                SelectShadowCasterLodVariants(chain, 9, 5.0f, texels).size() == 1,
                "A caster without a usable radius or LOD got variants");
        std::cout << "Virtual shadow clipmap policy passed\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
