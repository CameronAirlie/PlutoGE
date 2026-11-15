// Detail grass (docs/DETAIL_GRASS.md): the CPU half the GPU relies on. The lattice's ring dispatch must evaluate every
// candidate that grows exactly once (emulated here against a brute-force walk of the whole lattice), density must thin
// as the layer says, the clump mesh must be sane at every LOD, and the scene component must round-trip and follow its
// terrain's drawn surface.
#include "PlutoGE/render/DetailGrass.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    render::DetailGrassLayer TestLayer()
    {
        render::DetailGrassLayer layer;
        layer.density = 3.0f;              // a coarse lattice keeps the brute-force walk small
        layer.fullDensityDistance = 6.0f;
        layer.drawDistance = 50.0f;
        layer.seed = 7;
        return layer.Sanitized();
    }

    // The distance the GPU measures for a candidate: from the camera to the clump's root on a flat ground at y = 0.
    float Distance(const render::DetailGrassLattice &lattice, std::int32_t x, std::int32_t z, std::uint32_t seed,
                   const glm::vec3 &camera)
    {
        const auto position = lattice.Position(x, z, seed);
        return glm::length(glm::vec3(position.x, 0.0f, position.y) - camera);
    }

    // The clumps that grow, found by walking every lattice point that could reach the draw distance.
    std::set<std::pair<int, int>> BruteForce(const render::DetailGrassLattice &lattice, std::uint32_t seed, const glm::vec3 &camera)
    {
        std::set<std::pair<int, int>> grown;
        const float reach = lattice.drawDistance + lattice.margin;
        const int low = static_cast<int>(std::floor((std::min(camera.x, camera.z) - reach) / lattice.spacing)) - 1;
        const int high = static_cast<int>(std::ceil((std::max(camera.x, camera.z) + reach) / lattice.spacing)) + 1;
        for (int z = low; z <= high; ++z)
            for (int x = low; x <= high; ++x)
                if (lattice.Grows(x, z, seed, Distance(lattice, x, z, seed, camera), 1.0f))
                    grown.insert({x, z});
        return grown;
    }

    // The GPU's dispatch: ring k walks its own lattice of every 2^k-th point from RingOrigin, and a candidate counts only
    // in the ring its kept share falls in. Each grown candidate is counted as often as rings produce it.
    std::map<std::pair<int, int>, int> Dispatched(const render::DetailGrassLattice &lattice, std::uint32_t seed, const glm::vec3 &camera)
    {
        std::map<std::pair<int, int>, int> grown;
        for (std::uint32_t ring = 0; ring < lattice.rings; ++ring)
        {
            const auto origin = lattice.RingOrigin(ring, {camera.x, camera.z});
            const int stride = 1 << ring;
            for (std::uint32_t row = 0; row < lattice.ringSide[ring]; ++row)
                for (std::uint32_t column = 0; column < lattice.ringSide[ring]; ++column)
                {
                    const int x = (origin.x + static_cast<int>(column)) * stride, z = (origin.y + static_cast<int>(row)) * stride;
                    const float distance = Distance(lattice, x, z, seed, camera);
                    if (!(distance < lattice.drawDistance) || lattice.RingOf(lattice.Keep(distance)) != ring) continue;
                    if (lattice.Grows(x, z, seed, distance, 1.0f)) ++grown[{x, z}];
                }
        }
        return grown;
    }

    void LatticeLayout()
    {
        const auto layer = TestLayer();
        const auto lattice = render::MakeDetailGrassLattice(layer);
        Require(std::abs(lattice.spacing - 1.0f / std::sqrt(layer.density)) < 1.0e-5f, "Spacing follows the density");
        Require(lattice.rings >= 2 && lattice.rings <= render::DetailGrassMaxRings, "A long draw distance needs several rings");
        Require(lattice.ringRadius[lattice.rings - 1] == layer.drawDistance, "The last ring reaches the draw distance");
        for (std::uint32_t ring = 0; ring + 1 < lattice.rings; ++ring)
        {
            Require(lattice.ringRadius[ring] < lattice.ringRadius[ring + 1], "Rings grow outward");
            // Just inside a ring's edge the kept share still falls in that ring; just past it, in the next.
            const float edge = lattice.ringRadius[ring];
            Require(lattice.RingOf(lattice.Keep(edge * 0.999f)) == ring && lattice.RingOf(lattice.Keep(edge * 1.001f)) == ring + 1,
                    "Ring " + std::to_string(ring) + " ends where its kept share does");
        }
        Require(lattice.Keep(0.5f * layer.fullDensityDistance) == 1.0f && lattice.Keep(layer.fullDensityDistance) == 1.0f,
                "Everything grows within the full-density distance");
        Require(std::abs(lattice.Keep(2.0f * layer.fullDensityDistance) - 0.25f) < 1.0e-5f, "Falloff 2 quarters per doubling");

        // Every ring dispatches about as many candidates as the first, however far it reaches.
        for (std::uint32_t ring = 1; ring < lattice.rings; ++ring)
            Require(lattice.ringSide[ring] <= lattice.ringSide[0] + 4, "Ring " + std::to_string(ring) + " dispatches no more than the first");

        // Points of level L rank in [4^-(L+1), 4^-L), so thinning removes the finest levels first.
        for (int z = -9; z <= 9; ++z)
            for (int x = -9; x <= 9; ++x)
            {
                const auto level = lattice.Level(x, z);
                const float rank = lattice.Rank(x, z, layer.seed);
                const float top = std::ldexp(1.0f, -2 * static_cast<int>(level));
                Require(rank >= 0.0f && rank < top && (level == lattice.TopLevel() || rank >= 0.25f * top),
                        "Rank of (" + std::to_string(x) + ", " + std::to_string(z) + ") is outside its level's band");
            }

        // An absurdly dense, wide layer is spread to stay within the candidate budget instead of flooding the GPU.
        auto heavy = layer;
        heavy.density = 64.0f;
        heavy.fullDensityDistance = 60.0f;
        heavy.densityFalloff = 1.5f;
        heavy.drawDistance = 300.0f;
        const auto spread = render::MakeDetailGrassLattice(heavy);
        std::uint64_t candidates = 0;
        for (std::uint32_t ring = 0; ring < spread.rings; ++ring) candidates += std::uint64_t(spread.ringSide[ring]) * spread.ringSide[ring];
        Require(candidates <= (1u << 21) && spread.spacing > 1.0f / 8.0f, "A heavy layer stays within the candidate budget");
    }

    // The property the GPU relies on: the ring dispatch produces exactly the brute-force set, each clump once.
    void RingsEvaluateEachCandidateOnce()
    {
        const auto layer = TestLayer();
        const auto lattice = render::MakeDetailGrassLattice(layer);
        for (const glm::vec3 camera : {glm::vec3(0.3f, 1.7f, 0.4f), glm::vec3(-37.25f, 4.0f, 18.6f), glm::vec3(12.0f, 30.0f, -3.3f)})
        {
            const auto expected = BruteForce(lattice, layer.seed, camera);
            const auto dispatched = Dispatched(lattice, layer.seed, camera);
            Require(!expected.empty(), "Something grows around the camera");
            std::size_t duplicates = 0;
            for (const auto &[point, count] : dispatched) duplicates += count > 1 ? 1 : 0;
            Require(duplicates == 0, "A clump was produced by more than one ring");
            std::set<std::pair<int, int>> produced;
            for (const auto &[point, count] : dispatched) produced.insert(point);
            Require(produced == expected, "The rings missed " + std::to_string(expected.size() - std::min(expected.size(), produced.size())) +
                                              " clumps the lattice grows (of " + std::to_string(expected.size()) + ")");

            // Near the camera every candidate grows, at the layer's density.
            std::size_t near = 0;
            const float radius = layer.fullDensityDistance - lattice.margin;
            for (const auto &point : expected)
            {
                const auto position = lattice.Position(point.first, point.second, layer.seed);
                near += glm::length(glm::vec2(position.x - camera.x, position.y - camera.z)) < radius ? 1u : 0u;
            }
            if (camera.y < radius * 0.5f)
            {
                const float area = 3.14159265f * radius * radius;
                const float measured = float(near) / area;
                Require(std::abs(measured - layer.density) < 0.15f * layer.density,
                        "Near density " + std::to_string(measured) + " should be about " + std::to_string(layer.density));
            }
        }
    }

    // A half-painted density map keeps about half the candidates; nothing grows below the threshold.
    void DensityThinning()
    {
        const auto layer = TestLayer();
        const auto lattice = render::MakeDetailGrassLattice(layer);
        std::size_t full = 0, half = 0, none = 0;
        for (int z = -40; z < 40; ++z)
            for (int x = -40; x < 40; ++x)
            {
                full += lattice.Grows(x, z, layer.seed, 1.0f, 1.0f) ? 1u : 0u;
                half += lattice.Grows(x, z, layer.seed, 1.0f, 0.5f) ? 1u : 0u;
                none += lattice.Grows(x, z, layer.seed, 1.0f, 0.0f) ? 1u : 0u;
            }
        Require(full == 80u * 80u, "Full density grows every near candidate");
        Require(std::abs(float(half) / float(full) - 0.5f) < 0.05f, "Half density grows about half: " + std::to_string(half));
        Require(none == 0, "Zero density grows nothing");
        Require(render::DetailGrassLattice::Fade(1.0f, 0.2f) == 1.0f && render::DetailGrassLattice::Fade(0.2f, 0.2f) == 0.0f,
                "A clump shrinks away as the kept share reaches its rank");
    }

    void ClumpMesh()
    {
        render::DetailGrassClumpShape shape;
        const auto mesh = render::BuildDetailGrassClump(shape);
        Require(!mesh.vertices.empty() && mesh.indices.size() % 3 == 0, "A clump has triangles");
        for (const auto index : mesh.indices) Require(index < mesh.vertices.size(), "Clump indices stay in range");
        float top = 0.0f, bottom = 1.0f;
        for (const auto &vertex : mesh.vertices)
        {
            top = std::max(top, vertex.position.y);
            bottom = std::min(bottom, vertex.position.y);
            Require(std::abs(glm::length(vertex.normal) - 1.0f) < 1.0e-3f, "Clump normals are unit length");
        }
        Require(bottom == 0.0f && top <= 1.0f + 1.0e-5f && top > shape.minBladeHeight, "A clump is one unit tall, rooted at 0");
        Require(mesh.radius > 0.0f && mesh.radius < shape.radius + shape.lean, "The clump's radius bounds its blades");
        std::uint32_t covered = 0;
        for (std::uint32_t lod = 0; lod < render::DetailGrassLodCount; ++lod)
        {
            const auto &range = mesh.lods[lod];
            Require(range.firstIndex == covered && range.indexCount > 0 && range.indexCount % 3 == 0, "LODs tile the index buffer");
            covered += range.indexCount;
            if (lod > 0)
                Require(range.indexCount < mesh.lods[lod - 1].indexCount, "Each LOD is coarser than the last");
        }
        Require(covered == mesh.indices.size(), "Every index belongs to a LOD");

        auto single = shape;
        single.blades = 1;
        Require(render::BuildDetailGrassClump(single).lods[2].indexCount == 3, "One blade at the coarsest LOD is one triangle");
        Require(render::DetailGrassClumpShape{.blades = 0, .radius = -1.0f}.Sanitized().blades == 1, "Shapes sanitize");
    }

    void PresetsAndEstimate()
    {
        for (std::size_t index = 0; index < std::size_t(render::DetailGrassPreset::Count); ++index)
        {
            const auto preset = static_cast<render::DetailGrassPreset>(index);
            const auto layer = render::DetailGrassPresetLayer(preset);
            Require(layer == layer.Sanitized(), std::string("Preset is already sanitized: ") + render::DetailGrassPresetName(preset));
            Require(layer.name == render::DetailGrassPresetName(preset) && layer.wind.enabled, "Presets are named and sway");
        }

        // Applying a preset replaces the look and keeps where the layer grows.
        auto placed = TestLayer();
        placed.name = "Mine";
        placed.enabled = false;
        placed.weightThreshold = 0.6f;
        placed.maxSlopeDegrees = 20.0f;
        placed.shape.seed = 42;
        const auto lawn = render::DetailGrassApplyPreset(placed, render::DetailGrassPreset::Lawn);
        const auto reference = render::DetailGrassPresetLayer(render::DetailGrassPreset::Lawn);
        Require(lawn.name == "Mine" && !lawn.enabled && lawn.weightThreshold == 0.6f && lawn.maxSlopeDegrees == 20.0f &&
                    lawn.seed == placed.seed && lawn.shape.seed == 42,
                "A preset keeps the layer's name, state, placement and seeds");
        Require(lawn.density == reference.density && lawn.maxHeight == reference.maxHeight && lawn.color == reference.color &&
                    lawn.shape.blades == reference.shape.blades,
                "A preset sets the look");

        // With falloff 2: density * pi * R0^2 * (1 + 2 ln(D / R0)) clumps around the camera.
        const auto layer = TestLayer();
        const auto estimate = render::EstimateDetailGrass(layer);
        const float r0 = layer.fullDensityDistance, d = layer.drawDistance;
        const float expected = estimate.clumpsPerSquareMetre * glm::pi<float>() * r0 * r0 * (1.0f + 2.0f * std::log(d / r0));
        Require(std::abs(estimate.Clumps() / expected - 1.0f) < 0.02f,
                "The estimate integrates the kept share: " + std::to_string(estimate.Clumps()) + " vs " + std::to_string(expected));
        Require(std::abs(estimate.clumpsPerSquareMetre - layer.density) < 0.05f * layer.density, "An uncapped layer keeps its density");
        const auto mesh = render::BuildDetailGrassClump(layer.shape);
        for (std::uint32_t lod = 0; lod < render::DetailGrassLodCount; ++lod)
            Require(estimate.trianglesPerClump[lod] == mesh.lods[lod].indexCount / 3 && estimate.clumps[lod] > 0.0f,
                    "Every LOD is counted with its clump's triangles");
        Require(estimate.candidates > 0 && estimate.Triangles() > estimate.Clumps(), "Candidates and triangles are reported");

        auto huge = layer;
        huge.density = 64.0f; // every clump within 60 m: millions of candidates
        huge.fullDensityDistance = 60.0f;
        huge.drawDistance = 300.0f;
        Require(render::EstimateDetailGrass(huge).clumpsPerSquareMetre < 64.0f, "The estimate reports the candidate cap");
    }

    void ComponentRoundTrip()
    {
        DetailGrassComponent component;
        Require(component.GetLayers().size() == 1, "A new component grows one default layer");
        DetailGrassComponent::Layer meadow;
        meadow.settings.name = "Meadow";
        meadow.settings.density = 22.0f;
        meadow.settings.minHeight = 0.2f;
        meadow.settings.maxHeight = 0.9f;
        meadow.settings.color = {0.1f, 0.5f, 0.2f};
        meadow.settings.wind.enabled = false;
        meadow.settings.shape.blades = 9;
        meadow.settings.lod1Distance = 10.0f;
        meadow.settings.lod2Distance = 25.0f;
        meadow.densityLayer = "Forest floor";
        meadow.materialAsset = "asset://0123#0";
        meadow.settings.minAltitude = 2.0f;
        meadow.settings.maxAltitude = 120.0f;
        meadow.settings.altitudeFade = 6.0f;
        meadow.settings.clustering = 0.4f;
        DetailGrassComponent::Layer flowers;
        flowers.settings.name = "Flowers";
        flowers.settings.enabled = false;
        flowers.settings.density = 1.5f;
        component.SetLayers({meadow, flowers});

        DetailGrassComponent loaded;
        loaded.Deserialize(component.Serialize());
        Require(loaded.GetLayers().size() == 2, "Layers round-trip");
        const auto &a = component.GetLayers()[0], &b = loaded.GetLayers()[0];
        Require(b.settings.name == "Meadow" && b.densityLayer == "Forest floor" && b.materialAsset == "asset://0123#0",
                "Layer names and references round-trip");
        Require(std::abs(b.settings.density - 22.0f) < 1.0e-4f && std::abs(b.settings.maxHeight - 0.9f) < 1.0e-4f &&
                    glm::length(b.settings.color - a.settings.color) < 1.0e-4f && !b.settings.wind.enabled &&
                    b.settings.shape.blades == 9 && std::abs(b.settings.lod2Distance - 25.0f) < 1.0e-4f,
                "Layer settings round-trip");
        Require(b.settings.minAltitude == 2.0f && b.settings.maxAltitude == 120.0f && b.settings.altitudeFade == 6.0f &&
                    std::abs(b.settings.clustering - 0.4f) < 1.0e-4f,
                "The height band and bare patches round-trip");
        Require(loaded.GetLayers()[1].settings.minAltitude == -10000.0f && loaded.GetLayers()[1].settings.clustering == 0.0f,
                "Layers without them grow at every height, without gaps");
        Require(!loaded.GetLayers()[1].settings.enabled, "Disabled layers stay disabled");
        render::DetailGrassLayer inverted;
        inverted.minAltitude = 50.0f;
        inverted.maxAltitude = 10.0f;
        inverted.clustering = 3.0f;
        Require(inverted.Sanitized().maxAltitude >= inverted.Sanitized().minAltitude && inverted.Sanitized().clustering == 1.0f,
                "Height bands stay ordered and clustering in range");

        // Out-of-range values are sanitized rather than trusted.
        loaded.Deserialize({{"Layer.0.Density", PropertyType::Float, "100000"}, {"Layer.0.Height", PropertyType::Vec2, "2,0.5"}});
        Require(loaded.GetLayers()[0].settings.density <= 64.0f, "Density is clamped");
        Require(loaded.GetLayers()[0].settings.maxHeight >= loaded.GetLayers()[0].settings.minHeight, "Height ranges stay ordered");
        loaded.Deserialize({{"LayerCount", PropertyType::Int, "0"}});
        Require(loaded.GetLayers().empty(), "A count of zero removes the layers");
    }

    void HeightFieldFollowsTerrain()
    {
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
        auto *grass = entity->CreateComponent<DetailGrassComponent>();
        Require(!grass->GetHeightField(), "Without a terrain there is nothing to grow on");
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 9, .depth = 7, .cellSize = 2.0f});
        std::string samples;
        for (int index = 0; index < 9 * 7; ++index) samples += (index ? "," : "") + std::to_string(float(index % 9) * 0.5f);
        terrain->Deserialize({{"HeightSamples", PropertyType::String, samples}});

        const auto field = grass->GetHeightField();
        Require(field && field->Valid() && field->columns == 9 && field->rows == 7 && field->cellSize == 2.0f, "The field matches the grid");
        for (int z = 0; z < 7; ++z)
            for (int x = 0; x < 9; ++x)
                Require(field->heights[std::size_t(z) * 9 + std::size_t(x)] == terrain->GetSurfaceSampleHeight(x, z),
                        "Grass stands on the drawn (smoothed) surface");
        Require(grass->GetHeightField() == field, "An unchanged terrain shares its field");

        terrain->SetHole(3, 2, true);
        const auto holed = grass->GetHeightField();
        Require(holed != field && holed->revision != field->revision, "Cutting a hole makes a new field");
        Require(holed->holes[2 * 9 + 3] == 1 && holed->holes[2 * 9 + 4] == 0, "The cut cell is marked, its neighbour is not");
    }
}

int main()
try
{
    LatticeLayout();
    RingsEvaluateEachCandidateOnce();
    DensityThinning();
    ClumpMesh();
    PresetsAndEstimate();
    ComponentRoundTrip();
    HeightFieldFollowsTerrain();
    std::cout << "Detail grass tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
