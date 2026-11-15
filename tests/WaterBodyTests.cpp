// Water bodies without a GPU: lake shore tracing and polygon simplification, rivers conformed to the terrain and
// carved into it, and the water queries gameplay uses (oceans with area masks, lakes, rivers).
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/WaterBodies.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/RiverComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-3f) { return std::abs(a - b) <= tolerance; }
    constexpr float Pi = 3.14159265f;

    // A bowl centred on (20, 20) samples, with a second, separate dip at (36, 6).
    TerrainHeightField Basin()
    {
        TerrainHeightField field{41, 41, std::vector<float>(41 * 41)};
        for (int z = 0; z < 41; ++z)
            for (int x = 0; x < 41; ++x)
            {
                const float r = std::hypot(x - 20.0f, z - 20.0f);
                const float other = std::hypot(x - 36.0f, z - 6.0f);
                field.At(x, z) = std::min(r * 0.5f, 2.0f + other * 0.5f);
            }
        return field;
    }

    void LakeOutline()
    {
        const auto basin = Basin();
        // Level 4 floods a disc of radius 8 samples (2 m per sample: 16 m).
        WaterOutlineOptions options;
        options.shoreMargin = 0.0f;
        const auto outline = TraceWaterOutline(basin, 2.0f, 4.0f, {40.0f, 40.0f}, options);
        Require(outline.size() >= 3 && outline.size() <= 32, "The outline fits the ocean's 32 points: " + std::to_string(outline.size()));
        Require(PolygonArea(outline) > 0.0f, "Outlines run counter-clockwise");
        Require(PointInPolygon(outline, {40.0f, 40.0f}) && !PointInPolygon(outline, {72.0f, 12.0f}),
                "The outline holds the seed and not the other dip");
        const float expected = Pi * 16.0f * 16.0f;
        Require(std::abs(PolygonArea(outline) - expected) < expected * 0.2f,
                "The lake is about a 16 m disc: " + std::to_string(PolygonArea(outline)));

        options.shoreMargin = 2.0f;
        const auto wider = TraceWaterOutline(basin, 2.0f, 4.0f, {40.0f, 40.0f}, options);
        Require(PolygonArea(wider) > PolygonArea(outline), "A shore margin reaches under the banks");

        Require(TraceWaterOutline(basin, 2.0f, 4.0f, {0.0f, 80.0f}).empty(), "A seed above the level has no lake");
        Require(TraceWaterOutline(basin, 2.0f, 4.0f, {500.0f, 500.0f}).empty(), "A seed off the grid has no lake");
        // A level above the whole rim floods to the grid's edge and stops there.
        const auto flood = TraceWaterOutline(basin, 2.0f, 100.0f, {40.0f, 40.0f}, options);
        for (const auto &point : flood)
            Require(point.x >= 0.0f && point.y >= 0.0f && point.x <= 80.0f && point.y <= 80.0f, "Outlines stay on the grid");
    }

    void Polygons()
    {
        std::vector<glm::vec2> circle;
        for (int i = 0; i < 200; ++i)
            circle.push_back({10.0f * std::cos(i * 2.0f * Pi / 200.0f), 10.0f * std::sin(i * 2.0f * Pi / 200.0f)});
        const auto simple = SimplifyPolygon(circle, 16);
        Require(simple.size() <= 16 && simple.size() >= 8, "Simplified to the budget: " + std::to_string(simple.size()));
        Require(std::abs(PolygonArea(simple) - PolygonArea(circle)) < PolygonArea(circle) * 0.05f, "Simplifying keeps the shape");
        const std::vector<glm::vec2> square{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        Require(SimplifyPolygon(square, 32) == square, "Small polygons are untouched");
        Require(PointInPolygon(square, {2, 2}) && !PointInPolygon(square, {5, 2}) && Near(PolygonArea(square), 16.0f), "Square tests");
    }

    void Rivers()
    {
        // Ground falling along x with a 3 m bump at x = 20: the river must not flow up over it.
        const auto ground = [](glm::vec2 p) -> std::optional<float> {
            if (p.x > 100.0f)
                return std::nullopt;
            return 50.0f - p.x * 0.2f + (std::abs(p.x - 20.0f) < 2.0f ? 3.0f : 0.0f);
        };
        const std::vector<glm::vec3> path{{0, 0, 0}, {10, 0, 0}, {20, 0, 0}, {30, 0, 0}, {120, 7, 0}};
        const auto conformed = ConformRiverPath(path, ground, 0.5f);
        Require(Near(conformed[0].y, 49.5f) && Near(conformed[1].y, 47.5f), "Points sit below the ground");
        Require(Near(conformed[2].y, 47.5f), "The bump does not lift the river");
        Require(Near(conformed[3].y, 43.5f), "Below the bump it follows the ground again");
        Require(Near(conformed[4].y, 7.0f), "Off the terrain a point keeps its height (it is already lower)");
        for (std::size_t i = 1; i < conformed.size(); ++i)
            Require(conformed[i].y <= conformed[i - 1].y, "Rivers only flow downhill");

        TerrainHeightField flat{41, 41, std::vector<float>(41 * 41, 5.0f)};
        const std::vector<glm::vec3> bed{{0.0f, 4.7f, 20.0f}, {40.0f, 4.7f, 20.0f}};
        const auto changed = CarveRiverBed(flat, 1.0f, bed, {.halfWidth = 3.0f, .depth = 1.5f, .bankWidth = 4.0f});
        Require(!changed.Empty() && changed.z0 >= 13 && changed.z1 <= 28, "The carve stays within water and banks");
        Require(Near(flat.At(20, 20), 3.2f), "The centre is the full depth below the surface: " + std::to_string(flat.At(20, 20)));
        Require(flat.At(20, 22) > flat.At(20, 20) && flat.At(20, 22) < 4.7f, "The channel rises towards its edges");
        Require(flat.At(20, 25) > 4.7f && flat.At(20, 25) < 5.0f, "The banks rise back to the terrain");
        Require(flat.At(20, 30) == 5.0f && flat.At(20, 10) == 5.0f, "Beyond the banks nothing changes");
        // Carving again keeps the channel (the banks blend towards the water a little further).
        CarveRiverBed(flat, 1.0f, bed, {.halfWidth = 3.0f, .depth = 1.5f, .bankWidth = 4.0f});
        Require(Near(flat.At(20, 20), 3.2f) && flat.At(20, 30) == 5.0f, "A second carve keeps the channel and the land beyond");
    }

    void Queries()
    {
        Scene scene;
        auto *lakeEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Lake"}));
        lakeEntity->SetPosition({100.0f, 10.0f, 100.0f});
        auto *lake = lakeEntity->CreateComponent<OceanComponent>();
        lake->ApplyLakePreset();
        lake->SetAreas({{{{-5.0f, -5.0f}, {5.0f, -5.0f}, {5.0f, 5.0f}, {-5.0f, 5.0f}}}});

        const auto inside = SampleWaterSurface(scene, {102.0f, 0.0f, 98.0f});
        Require(inside && inside->kind == WaterBodyKind::Ocean && Near(inside->height, 10.0f, 0.1f), "Inside the lake's area");
        Require(!SampleWaterSurface(scene, {110.0f, 0.0f, 100.0f}), "Outside the lake is dry");
        Require(IsUnderwater(scene, {101.0f, 8.0f, 101.0f}) && !IsUnderwater(scene, {101.0f, 12.0f, 101.0f}), "Under and over the lake");

        // The sea: no areas, not inverted, covers everything; areas carve holes in it.
        auto *seaEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Sea"}));
        auto *sea = seaEntity->CreateComponent<OceanComponent>();
        sea->SetAreas({{{{-20.0f, -20.0f}, {20.0f, -20.0f}, {20.0f, 20.0f}, {-20.0f, 20.0f}}}});
        const auto open = SampleWaterSurface(scene, {500.0f, 0.0f, 500.0f});
        Require(open && Near(open->height, 0.0f, 1.0f), "The sea is everywhere outside its holes");
        Require(!SampleWaterSurface(scene, {0.0f, 0.0f, 0.0f}), "A hole in the sea is dry");
        Require(SampleWaterSurface(scene, {102.0f, 0.0f, 98.0f})->height > 5.0f, "Where they overlap, the higher body wins");

        // A river along x at 3 m, 6 m wide, on a terrain.
        auto *terrainEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Ground"}));
        terrainEntity->SetPosition({-50.0f, 0.0f, 200.0f});
        auto *terrain = terrainEntity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 101, .depth = 41, .cellSize = 1.0f, .heightScale = 50.0f});
        TerrainHeightData slope;
        slope.field = {101, 41, std::vector<float>(101 * 41)};
        for (int z = 0; z < 41; ++z)
            for (int x = 0; x < 101; ++x)
                slope.field.At(x, z) = 20.0f - x * 0.1f;
        terrain->SetHeightData(slope);
        terrain->Deserialize({{"SurfaceSmoothing", PropertyType::Float, "0"}});

        auto *riverEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "River"}));
        auto *spline = riverEntity->CreateComponent<SplineComponent>(SplineComponentConfig{
            .points = {{{-40.0f, 30.0f, 220.0f}}, {{0.0f, 30.0f, 220.0f}}, {{40.0f, 30.0f, 220.0f}}},
            .width = 6.0f,
            .closed = false,
        });
        auto *river = riverEntity->CreateComponent<RiverComponent>();
        river->SetSurfaceDrop(0.5f);
        river->ConfigureSpline();
        Require(!spline->IsClosed() && !spline->ShouldGenerateCollision(), "A river spline is open and has no collision");
        Require(river->ConformToTerrain(), "The river finds the terrain");
        // World x -40 is terrain x 10: ground 19, water 18.5; x 40 is terrain x 90: ground 11.
        Require(Near(spline->GetPoints()[0].position.y, 18.5f, 0.01f) && Near(spline->GetPoints()[2].position.y, 10.5f, 0.01f),
                "Conformed points sit below the ground");
        const auto water = SampleWaterSurface(scene, {0.0f, 0.0f, 222.0f});
        Require(water && water->kind == WaterBodyKind::River && water->body == river, "Inside the river's width");
        Require(Near(water->height, 14.5f, 0.05f) && water->flow.x > 0.9f, "River height and flow: " + std::to_string(water->height));
        const auto beside = SampleWaterSurface(scene, {0.0f, 0.0f, 226.0f});
        Require(!beside || beside->kind != WaterBodyKind::River, "Beside the river is not river (the sea is there)");

        const float before = terrain->GetHeightSamples()[20 * 101 + 50];
        const auto carved = river->CarveTerrain();
        Require(carved.size() == 1 && carved[0].terrain == terrain && !carved[0].before.rect.Empty(), "One terrain was carved");
        Require(terrain->GetHeightSamples()[20 * 101 + 50] < before - 1.5f, "The bed is below the water");
        terrain->RestoreHeights(carved[0].before);
        Require(Near(terrain->GetHeightSamples()[20 * 101 + 50], before), "The before-region undoes the carve");

        Scene copyScene;
        auto *copy = copyScene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Copy"}))->CreateComponent<RiverComponent>();
        river->SetDepth(2.5f);
        copy->Deserialize(river->Serialize());
        Require(copy->GetDepth() == 2.5f && copy->GetSurfaceDrop() == 0.5f && !copy->GetSpline(), "The river round-trips");
    }
}

int main()
{
    try
    {
        LakeOutline();
        Polygons();
        Rivers();
        Queries();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Water body tests passed.\n";
    return 0;
}
