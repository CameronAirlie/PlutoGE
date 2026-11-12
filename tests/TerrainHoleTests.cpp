// Terrain holes without a GPU: the hole mask (editing, brush, encoding), the terrain's persistence of it, its surface
// queries and CPU raycast, the render mesh at every LOD, and Bullet collision through and beside a hole.
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TerrainHoleMask.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
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

    std::string FlatSamples(int width, int depth, float height)
    {
        std::string text;
        for (int i = 0; i < width * depth; ++i)
            text += (i ? "," : "") + std::to_string(height);
        return text;
    }

    std::string Property(const std::vector<scene::Property> &properties, const std::string &name)
    {
        for (const auto &property : properties)
            if (property.name == name)
                return property.Text();
        throw std::runtime_error("No property " + name);
    }

    void MaskEditing()
    {
        TerrainHoleMask mask(8, 6);
        Require(mask.Empty() && mask.GetCellsX() == 8 && mask.GetCellsZ() == 6, "A new mask has no holes");
        Require(mask.Set(2, 3, true) && !mask.Set(2, 3, true), "Set reports only real changes");
        Require(mask.IsHole(2, 3) && !mask.IsHole(3, 3) && mask.GetHoleCount() == 1, "One cell is cut");
        Require(!mask.IsHole(-1, 0) && !mask.IsHole(8, 0) && !mask.Set(8, 0, true), "Outside the mask is never a hole");
        Require(mask.IsHoleAt(2.0f, 3.0f) && mask.IsHoleAt(2.99f, 3.5f) && !mask.IsHoleAt(3.0f, 3.5f),
                "A point belongs to the cell it lies in");
        Require(mask.AnyInRect(0, 0, 3, 4) && !mask.AnyInRect(0, 0, 2, 8) && !mask.AnyInRect(3, 0, 8, 6),
                "Rect queries are half-open");

        mask.Resize(4, 4);
        Require(mask.IsHole(2, 3) && mask.GetHoleCount() == 1, "Resizing keeps the overlapping cells");
        mask.Resize(2, 2);
        Require(mask.Empty(), "Resizing drops cells outside the new size");

        TerrainHoleMask brush(10, 10);
        Require(brush.PaintCircle(5.0f, 5.0f, 1.0f, true), "A brush dab cuts");
        Require(brush.IsHole(4, 4) && brush.IsHole(5, 5) && brush.IsHole(4, 5) && brush.IsHole(5, 4) &&
                    brush.GetHoleCount() == 4,
                "A dab cuts the cells whose centres it covers");
        Require(brush.PaintCircle(5.0f, 5.0f, 1.0f, false) && brush.Empty(), "Filling restores them");
        Require(brush.PaintCircle(7.2f, 1.7f, 0.01f, true) && brush.IsHole(7, 1) && brush.GetHoleCount() == 1,
                "A brush smaller than a cell still cuts the cell under it");
    }

    void MaskEncoding()
    {
        TerrainHoleMask empty(5, 5);
        Require(empty.Encode().empty(), "An empty mask encodes as nothing");
        const auto decodedEmpty = TerrainHoleMask::Decode("", 5, 5);
        Require(decodedEmpty && decodedEmpty->Empty() && decodedEmpty->GetCellsX() == 5, "Nothing decodes as an empty mask");

        TerrainHoleMask mask(5, 4);
        mask.Set(0, 0, true); // a leading hole needs a zero-length solid run
        mask.Set(3, 1, true);
        mask.Set(4, 1, true);
        mask.Set(0, 2, true);
        mask.Set(4, 3, true); // and a trailing one
        const std::string text = mask.Encode();
        Require(text == "5x4:0,1,7,3,8,1", "Unexpected encoding: " + text);
        const auto decoded = TerrainHoleMask::Decode(text, 5, 4);
        Require(decoded && *decoded == mask, "Encoding does not round-trip");

        Require(!TerrainHoleMask::Decode(text, 6, 4), "A mask of another size is rejected");
        Require(!TerrainHoleMask::Decode("5x4:0,1,7", 5, 4), "Runs that do not cover the mask are rejected");
        Require(!TerrainHoleMask::Decode("5x4:0,1,70", 5, 4), "Runs past the end are rejected");
        Require(!TerrainHoleMask::Decode("5x4;0,20", 5, 4) && !TerrainHoleMask::Decode("bogus", 5, 4) &&
                    !TerrainHoleMask::Decode("5x4:0,,20", 5, 4),
                "Malformed text is rejected");
    }

    void ComponentPersistence()
    {
        TerrainComponent terrain(TerrainComponentConfig{.width = 9, .depth = 9});
        Require(terrain.GetHoles().GetCellsX() == 8 && terrain.GetHoles().GetCellsZ() == 8, "The mask has a cell per quad");
        const auto before = terrain.GetHeavyDataRevision();
        Require(terrain.SetHole(4, 4, true) && terrain.GetHeavyDataRevision() != before, "Cutting bumps the heavy revision");

        const auto saved = terrain.Serialize();
        const std::string holeText = Property(saved, "HoleMask");
        Require(!holeText.empty(), "Holes are saved");

        TerrainComponent loaded;
        loaded.Deserialize(saved);
        Require(loaded.GetHoles() == terrain.GetHoles(), "Holes do not survive a save and load");

        // Snapshots without heavy data (the inspector's) leave the holes alone; an explicit empty mask clears them.
        loaded.DeserializeKeepingHeavyData({{"CellSize", PropertyType::Float, "1"}});
        Require(loaded.GetHoles().IsHole(4, 4), "A snapshot without HoleMask dropped the holes");
        loaded.Deserialize({{"HoleMask", PropertyType::String, ""}});
        Require(loaded.GetHoles().Empty(), "An empty HoleMask did not clear the holes");
        loaded.Deserialize({{"HoleMask", PropertyType::String, "garbage"}});
        Require(loaded.GetHoles().Empty(), "A malformed HoleMask was applied");

        // Scenes from before holes load without them.
        TerrainComponent legacy;
        legacy.Deserialize({{"Width", PropertyType::Int, "3"}, {"Depth", PropertyType::Int, "3"},
                            {"HeightSamples", PropertyType::String, "0,1,2,1,2,3,2,3,4"}});
        Require(legacy.GetHoles().Empty() && legacy.GetHoles().GetCellsX() == 2, "A legacy terrain has no holes");

        // Resizing the terrain keeps the overlapping holes.
        terrain.SetSize(6, 6);
        Require(terrain.GetHoles().GetCellsX() == 5 && terrain.GetHoles().IsHole(4, 4), "Resizing lost a hole");

        // The brush.
        TerrainComponent brushed(TerrainComponentConfig{.width = 9, .depth = 9, .cellSize = 2.0f});
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
        auto *painted = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 9, .depth = 9, .cellSize = 2.0f});
        painted->SetPaintEnabled(true);
        painted->SetPaintMode(TerrainPaintMode::Hole);
        painted->SetBrushRadius(0.5f);
        Require(painted->PaintAtWorldPosition({5.0f, 0.0f, 7.0f}, 0.016f) && painted->GetHoles().IsHole(2, 3) &&
                    painted->GetHoles().GetHoleCount() == 1,
                "The hole brush did not cut the cell under it");
        painted->SetPaintMode(TerrainPaintMode::Fill);
        Require(painted->PaintAtWorldPosition({5.0f, 0.0f, 7.0f}, 0.016f) && painted->GetHoles().Empty(),
                "The fill brush did not restore it");
        (void)brushed;
    }

    void SurfaceQueries()
    {
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
        entity->SetPosition({100.0f, 0.0f, 0.0f});
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 9, .depth = 9, .cellSize = 2.0f});
        terrain->Deserialize({{"HeightSamples", PropertyType::String, FlatSamples(9, 9, 3.0f)}});
        terrain->SetHole(4, 4, true); // local x and z 8..10

        float height = 0.0f;
        glm::vec3 normal{};
        Require(!terrain->TrySampleSurface(9.0f, 9.0f, height, normal), "A hole has no surface");
        Require(terrain->TrySampleSurface(7.0f, 9.0f, height, normal) && std::abs(height - 3.0f) < 0.001f,
                "The cell beside a hole lost its surface");
        Require(!terrain->TrySampleWorldSurface({109.0f, 50.0f, 9.0f}, height, normal) &&
                    terrain->TrySampleWorldSurface({107.0f, 50.0f, 9.0f}, height, normal),
                "World surface queries ignore holes");
        Require(terrain->IsHoleAtWorldPosition({109.0f, 0.0f, 9.0f}) && !terrain->IsHoleAtWorldPosition({107.0f, 0.0f, 9.0f}),
                "World hole queries are wrong");

        terrain->GetMeshForBaking(); // the CPU raycast needs a built terrain
        glm::vec3 hit{};
        Require(!terrain->Raycast({109.0f, 20.0f, 9.0f}, {0.0f, -1.0f, 0.0f}, hit), "The CPU raycast hit a hole");
        Require(terrain->Raycast({107.0f, 20.0f, 9.0f}, {0.0f, -1.0f, 0.0f}, hit) && std::abs(hit.y - 3.0f) < 0.01f,
                "The CPU raycast missed solid ground beside a hole");
        // A slanted ray entering through the hole goes on under the terrain rather than stopping at the hole.
        Require(!terrain->Raycast({109.0f, 4.0f, 9.0f}, glm::normalize(glm::vec3(0.0f, -1.0f, 0.05f)), hit),
                "A ray through the hole stopped at it");
    }

    void RenderMesh()
    {
        TerrainComponent terrain(TerrainComponentConfig{.width = 17, .depth = 17, .chunkSize = 8, .lodCount = 4});
        terrain.SetHole(5, 6, true);
        terrain.SetHole(12, 12, true);
        const auto *mesh = terrain.GetMeshForBaking();
        Require(mesh != nullptr, "No terrain mesh");
        const auto &data = mesh->GetMeshData();

        for (std::size_t s = 0; s < mesh->GetSubmeshCount(); ++s)
        {
            const auto &submesh = mesh->GetSubmesh(s);
            for (std::size_t lod = 0; lod < submesh.lods.size(); ++lod)
            {
                const auto &range = submesh.lods[lod];
                float area = 0.0f;
                for (std::uint32_t i = range.indexOffset; i < range.indexOffset + range.indexCount; i += 3)
                {
                    glm::vec3 corners[3];
                    for (int c = 0; c < 3; ++c)
                    {
                        const auto &position = data.vertices[data.indices[i + c]].position;
                        corners[c] = {position[0], position[1], position[2]};
                    }
                    const glm::vec3 centroid = (corners[0] + corners[1] + corners[2]) / 3.0f;
                    Require(!terrain.GetHoles().IsHoleAt(centroid.x, centroid.z),
                            "LOD " + std::to_string(lod) + " draws a triangle in a hole");
                    area += 0.5f * std::abs((corners[1].x - corners[0].x) * (corners[2].z - corners[0].z) -
                                            (corners[2].x - corners[0].x) * (corners[1].z - corners[0].z));
                }
                // Every LOD covers the chunk exactly, minus its holes: no cracks where coarse quads gave way to fine.
                const bool holed = s == 0 || s == 3; // chunks (0,0) and (1,1) of the 2x2 grid
                const float expected = 64.0f - (holed ? 1.0f : 0.0f);
                Require(std::abs(area - expected) < 0.01f, "LOD " + std::to_string(lod) + " of chunk " + std::to_string(s) +
                                                               " covers " + std::to_string(area) + " cells");
            }
        }
    }

    void Collision()
    {
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 9, .depth = 9, .cellSize = 2.0f});
        terrain->Deserialize({{"HeightSamples", PropertyType::String, FlatSamples(9, 9, 3.0f)}});
        terrain->SetStatic(true);
        entity->CreateComponent<ColliderComponent>()->SetShape(ColliderShape::Terrain);

        PhysicsRaycastHit hit;
        Require(scene.Raycast({9.0f, 20.0f, 9.0f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit) && std::abs(hit.point.y - 3.0f) < 0.01f,
                "A terrain without holes did not collide");

        // Cutting a hole rebuilds the collider: rays fall through it, but not through its neighbours.
        terrain->SetHole(4, 4, true);
        if (scene.Raycast({9.0f, 20.0f, 9.0f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit))
            throw std::runtime_error("A ray hit the terrain through a hole at " + std::to_string(hit.point.x) + "," + std::to_string(hit.point.y) + "," + std::to_string(hit.point.z));
        Require(scene.Raycast({7.0f, 20.0f, 9.0f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit) && std::abs(hit.point.y - 3.0f) < 0.01f &&
                    hit.entityId == entity->GetID(),
                "The ground beside a hole stopped colliding");
        Require(scene.Raycast({15.5f, 20.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit) && std::abs(hit.point.y - 3.0f) < 0.01f,
                "The holed collider is misplaced");

        // Filling it restores the ordinary heightfield.
        terrain->SetHole(4, 4, false);
        Require(scene.Raycast({9.0f, 20.0f, 9.0f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit), "Filling a hole did not restore collision");
    }

    // Half cuts: a cell keeps one triangle, so a rim can run diagonally.
    void HalfCuts()
    {
        Require(IsCutAt(TerrainHoleCut::CornerLowXLowZ, 0.2f, 0.2f) && !IsCutAt(TerrainHoleCut::CornerLowXLowZ, 0.8f, 0.8f) &&
                    IsCutAt(TerrainHoleCut::CornerHighXLowZ, 0.8f, 0.2f) && !IsCutAt(TerrainHoleCut::CornerHighXLowZ, 0.2f, 0.8f) &&
                    IsCutAt(TerrainHoleCut::CornerLowXHighZ, 0.2f, 0.8f) && !IsCutAt(TerrainHoleCut::CornerLowXHighZ, 0.8f, 0.2f) &&
                    IsCutAt(TerrainHoleCut::CornerHighXHighZ, 0.8f, 0.8f) && !IsCutAt(TerrainHoleCut::CornerHighXHighZ, 0.2f, 0.2f),
                "A half cut removes the triangle at its corner");

        TerrainHoleMask mask(6, 6);
        Require(mask.SetCut(1, 1, TerrainHoleCut::CornerHighXLowZ) && mask.GetHoleCount() == 1 && !mask.IsHole(1, 1) &&
                    mask.AnyInRect(1, 1, 2, 2),
                "A half cut counts as cut, but not as a whole hole");
        Require(mask.IsHoleAt(1.9f, 1.1f) && !mask.IsHoleAt(1.1f, 1.9f), "Point queries follow the diagonal");
        Require(mask.SetCut(1, 1, TerrainHoleCut::Full) && mask.GetHoleCount() == 1 && mask.IsHole(1, 1), "A half cut becomes whole");
        mask.SetCut(1, 1, TerrainHoleCut::CornerLowXHighZ);
        mask.Set(4, 2, true);
        const auto text = mask.Encode();
        Require(text.find(';') != std::string::npos, "A mask with half cuts uses the extended encoding: " + text);
        const auto decoded = TerrainHoleMask::Decode(text, 6, 6);
        Require(decoded && *decoded == mask, "Half cuts do not round-trip: " + text);
        Require(!TerrainHoleMask::Decode("6x6;0*30,9*6", 6, 6), "An unknown cut value is rejected");
        mask.SetCut(1, 1, TerrainHoleCut::None);
        Require(mask.Encode().find(':') != std::string::npos, "Whole cells alone keep the original encoding");

        // A square hole's corners: the solid cells diagonal to it stay, its edge-adjacent corners round off.
        TerrainHoleMask square(8, 8);
        for (int z = 2; z < 6; ++z)
            for (int x = 2; x < 6; ++x)
                square.Set(x, z, true);
        // Each corner of the square: both outer neighbours are solid, both inner ones cut, so it keeps a triangle.
        Require(square.SmoothEdges(), "Smoothing changed nothing");
        Require(square.GetCut(2, 2) == TerrainHoleCut::CornerHighXHighZ && square.GetCut(5, 5) == TerrainHoleCut::CornerLowXLowZ &&
                    square.GetCut(5, 2) == TerrainHoleCut::CornerLowXHighZ && square.GetCut(2, 5) == TerrainHoleCut::CornerHighXLowZ,
                "A hole's corner cells keep the triangle facing the ground");
        Require(square.IsHole(3, 2) && square.IsHole(3, 3) && square.GetCut(1, 2) == TerrainHoleCut::None,
                "Straight edges and cells outside are unchanged");
        // An L-shaped notch: the solid cell in its inner corner loses the triangle facing the hole.
        TerrainHoleMask notch(6, 6);
        notch.Set(1, 2, true);
        notch.Set(2, 1, true);
        notch.SmoothEdges();
        Require(notch.GetCut(2, 2) == TerrainHoleCut::CornerLowXLowZ, "A solid cell between two cut sides loses that corner");

        // Rendering and collision keep exactly the solid triangle.
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 9, .depth = 9, .cellSize = 2.0f});
        terrain->Deserialize({{"HeightSamples", PropertyType::String, FlatSamples(9, 9, 3.0f)}});
        terrain->SetStatic(true);
        entity->CreateComponent<ColliderComponent>()->SetShape(ColliderShape::Terrain);
        TerrainHoleMask cuts(8, 8);
        cuts.SetCut(4, 4, TerrainHoleCut::CornerHighXLowZ); // local x 8..10, z 8..10: the low-z, high-x half is gone
        cuts.SetCut(2, 4, TerrainHoleCut::CornerLowXLowZ);
        terrain->SetHoles(cuts);
        PhysicsRaycastHit hit;
        Require(!scene.Raycast({9.8f, 20.0f, 8.2f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit), "A ray hit the cut half of a cell");
        Require(scene.Raycast({8.2f, 20.0f, 9.8f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit) && std::abs(hit.point.y - 3.0f) < 0.01f,
                "The kept half of a cell stopped colliding");
        Require(!scene.Raycast({4.3f, 20.0f, 8.3f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit) &&
                    scene.Raycast({5.7f, 20.0f, 9.7f}, {0.0f, -1.0f, 0.0f}, 50.0f, hit),
                "The other diagonal's half cut collides wrongly");
        float height = 0.0f;
        glm::vec3 normal{};
        Require(!terrain->TrySampleSurface(9.8f, 8.2f, height, normal) && terrain->TrySampleSurface(8.2f, 9.8f, height, normal),
                "Surface queries ignore the diagonal");

        const auto *mesh = terrain->GetMeshForBaking();
        const auto &data = mesh->GetMeshData();
        const auto &range = mesh->GetSubmesh(0).lods.front();
        float area = 0.0f;
        for (std::uint32_t i = range.indexOffset; i < range.indexOffset + range.indexCount; i += 3)
        {
            glm::vec3 corners[3];
            for (int c = 0; c < 3; ++c)
            {
                const auto &position = data.vertices[data.indices[i + c]].position;
                corners[c] = {position[0], position[1], position[2]};
            }
            const glm::vec3 centroid = (corners[0] + corners[1] + corners[2]) / 3.0f;
            Require(!terrain->GetHoles().IsHoleAt(centroid.x / 2.0f, centroid.z / 2.0f), "A triangle is drawn in a cut half");
            Require(glm::cross(corners[1] - corners[0], corners[2] - corners[0]).y > 0.0f,
                    "A kept triangle is wound the wrong way");
            area += 0.5f * glm::length(glm::cross(corners[1] - corners[0], corners[2] - corners[0]));
        }
        const float cells = 8.0f * 8.0f * 4.0f; // one chunk of 8 x 8 cells, each 2 m square
        Require(std::abs(area - (cells - 2.0f * 2.0f)) < 0.01f, "Two half cuts remove one cell's area: " + std::to_string(area));
    }
}

int main()
{
    try
    {
        MaskEditing();
        MaskEncoding();
        ComponentPersistence();
        SurfaceQueries();
        RenderMesh();
        Collision();
        HalfCuts();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain hole tests passed.\n";
    return 0;
}
